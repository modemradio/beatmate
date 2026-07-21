#include "RekordboxMonitor.h"
#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <filesystem>
#include <functional>
#include <thread>

namespace fs = std::filesystem;

namespace BeatMate::Services::Realtime {

RekordboxMonitor::RekordboxMonitor() {}
RekordboxMonitor::~RekordboxMonitor() { stop(); }

void RekordboxMonitor::start(int intervalMs) { startTimer(intervalMs); spdlog::info("RekordboxMonitor: Started (interval={}ms)", intervalMs); }
void RekordboxMonitor::stop() { stopTimer(); }

void RekordboxMonitor::timerCallback() {
    if (dbPath_.empty() || !fs::exists(dbPath_))
        return;

    auto dbPath = dbPath_;
    auto lastHash = lastTrackHash_;
    auto callback = trackChangedCallback_;
    auto self = this;

    std::thread([dbPath, lastHash, callback, self]() {
        sqlite3* db = nullptr;
        int rc = sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
        if (rc != SQLITE_OK) {
            if (db) sqlite3_close(db);
            return;
        }

        const char* sql =
            "SELECT c.Title, c.ArtistName, c.BPM "
            "FROM djmdSongHistory h "
            "JOIN djmdContent c ON h.ContentID = c.ID "
            "ORDER BY h.created_at DESC LIMIT 1";

        sqlite3_stmt* stmt = nullptr;
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);

        if (rc != SQLITE_OK) {
            const char* sqlV5 =
                "SELECT c.title, c.artist, c.bpm "
                "FROM history_entry h "
                "JOIN content c ON h.content_id = c.rb_local_content_id "
                "ORDER BY h.played_at DESC LIMIT 1";
            rc = sqlite3_prepare_v2(db, sqlV5, -1, &stmt, nullptr);
        }

        if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
            const char* titleRaw  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* artistRaw = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            double bpm = sqlite3_column_double(stmt, 2);

            std::string title  = titleRaw  ? titleRaw  : "";
            std::string artist = artistRaw ? artistRaw : "";
            std::string currentHash = title + "|" + artist + "|" + std::to_string(bpm);

            if (currentHash != lastHash) {
                juce::MessageManager::callAsync([self, currentHash, title, artist, bpm, callback]() {
                    self->lastTrackHash_ = currentHash;
                    spdlog::info("RekordboxMonitor: Now playing '{}' by '{}' (BPM: {:.1f})", title, artist, bpm);
                    if (callback) {
                        callback(title, artist, bpm);
                    }
                });
            }
        }

        if (stmt) sqlite3_finalize(stmt);
        sqlite3_close(db);
    }).detach();
}

} // namespace BeatMate::Services::Realtime
