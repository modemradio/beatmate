#include "EngineDJLiveReader.h"

#include <juce_core/juce_core.h>
#include <spdlog/spdlog.h>
#include <sqlite3.h>

namespace BeatMate::Services::EngineDJ {

using BeatMate::Services::DJSoftware::PlayedTrack;

namespace {

static std::string colStr(sqlite3_stmt* st, int i) {
    const unsigned char* t = sqlite3_column_text(st, i);
    return t ? reinterpret_cast<const char*>(t) : std::string{};
}

static bool tableExists(sqlite3* db, const char* name) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?;",
            -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    bool found = (sqlite3_step(st) == SQLITE_ROW);
    sqlite3_finalize(st);
    return found;
}

static bool columnExists(sqlite3* db, const char* table, const char* column) {
    std::string q = "PRAGMA table_info(" + std::string(table) + ");";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, q.c_str(), -1, &st, nullptr) != SQLITE_OK) return false;
    bool found = false;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (colStr(st, 1) == column) { found = true; break; }
    }
    sqlite3_finalize(st);
    return found;
}

} // namespace

std::string EngineDJLiveReader::findEngineDbPath()
{
    juce::File user = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    const char* candidates[] = {
        "Music/Engine Library/Database2/m.db",
        "Documents/Engine Library/Database2/m.db",
    };
    for (auto* rel : candidates) {
        auto f = user.getChildFile(rel);
        if (f.existsAsFile()) return f.getFullPathName().toStdString();
    }
    return {};
}

bool EngineDJLiveReader::isPresent()
{
    return !findEngineDbPath().empty();
}

std::vector<PlayedTrack> EngineDJLiveReader::readRecentHistory(int maxTracks)
{
    std::vector<PlayedTrack> result;
    auto path = findEngineDbPath();
    if (path.empty()) return result;

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(("file:" + path + "?mode=ro").c_str(), &db,
                        SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return result;
    }

    std::string orderBy = "id DESC";
    if (columnExists(db, "Track", "lastPlayedTime")) orderBy = "lastPlayedTime DESC";
    else if (columnExists(db, "Track", "dateAdded"))  orderBy = "dateAdded DESC";

    std::string sql =
        "SELECT title, artist, bpmAnalyzed, length, path "
        "FROM Track WHERE title IS NOT NULL ORDER BY " + orderBy +
        " LIMIT " + std::to_string(maxTracks > 0 ? maxTracks : 200) + ";";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            PlayedTrack pt;
            pt.title       = colStr(st, 0);
            pt.artist      = colStr(st, 1);
            pt.bpm         = sqlite3_column_double(st, 2);
            pt.durationSec = sqlite3_column_double(st, 3);
            pt.filePath    = colStr(st, 4);
            pt.source      = "EngineDJ";
            result.push_back(std::move(pt));
        }
        sqlite3_finalize(st);
    }
    sqlite3_close(db);
    spdlog::info("[EngineDJ] read {} rows from m.db", result.size());
    return result;
}

std::optional<PlayedTrack> EngineDJLiveReader::readNowPlaying()
{
    auto path = findEngineDbPath();
    if (path.empty()) return std::nullopt;

    // Garde de fraicheur : DB active seulement si m.db-wal modifie recemment
    juce::File wal(path + "-wal");
    if (wal.existsAsFile()) {
        auto age = juce::Time::getCurrentTime().toMilliseconds() -
                   wal.getLastModificationTime().toMilliseconds();
        if (age > 120 * 1000) return std::nullopt;
    }

    auto hist = readRecentHistory(1);
    if (hist.empty()) return std::nullopt;
    return hist.front();
}

} // namespace BeatMate::Services::EngineDJ
