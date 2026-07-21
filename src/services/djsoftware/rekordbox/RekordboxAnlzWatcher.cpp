#include "RekordboxAnlzWatcher.h"
#include "RekordboxCipher.h"

#include <juce_core/juce_core.h>
#include <spdlog/spdlog.h>
#include <sqlite3.h>

#include <chrono>
#include <thread>

namespace BeatMate::Services::Rekordbox {

using BeatMate::Services::DJSoftware::PlayedTrack;

namespace {

static std::string masterDbPath() {
    auto appData = juce::File::getSpecialLocation(juce::File::windowsLocalAppData)
                       .getParentDirectory().getChildFile("Roaming");
    auto rb = appData.getChildFile("Pioneer").getChildFile("rekordbox");
    auto db = rb.getChildFile("master.db");
    if (db.existsAsFile()) return db.getFullPathName().toStdString();
    auto db6 = appData.getChildFile("Pioneer").getChildFile("rekordbox6").getChildFile("master.db");
    if (db6.existsAsFile()) return db6.getFullPathName().toStdString();
    return {};
}

static juce::File anlzRoot() {
    auto db = masterDbPath();
    if (db.empty()) return {};
    return juce::File(db).getSiblingFile("share").getChildFile("PIONEER").getChildFile("USBANLZ");
}

static constexpr const char* kRekordboxMasterKey =
    "402fd482c38817c35ffa8ffb8c7d93143b749e7d315df7a81732a1ff43608497";

static std::string colStr(sqlite3_stmt* st, int i) {
    const unsigned char* t = sqlite3_column_text(st, i);
    return t ? reinterpret_cast<const char*>(t) : std::string{};
}

static std::string snapshotMasterDb(const std::string& src) {
    juce::File s(src);
    if (!s.existsAsFile()) return src;
    juce::File dst = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("beatmate_anlz_snapshot.db");
    dst.deleteFile();
    s.copyFileTo(dst);
    juce::File walS(src + "-wal");
    juce::File shmS(src + "-shm");
    if (walS.existsAsFile()) {
        juce::File walD(dst.getFullPathName() + "-wal");
        walD.deleteFile();
        walS.copyFileTo(walD);
    }
    if (shmS.existsAsFile()) {
        juce::File shmD(dst.getFullPathName() + "-shm");
        shmD.deleteFile();
        shmS.copyFileTo(shmD);
    }
    return dst.getFullPathName().toStdString();
}

struct AnlzHit { juce::File file; int64_t mtimeMs = 0; };
static AnlzHit findNewestAnlz(const juce::File& root) {
    AnlzHit out;
    if (!root.isDirectory()) return out;
    auto files = root.findChildFiles(juce::File::findFiles, true,
                                     "ANLZ*.DAT;ANLZ*.EXT;ANLZ*.2EX");
    for (auto& f : files) {
        int64_t m = f.getLastModificationTime().toMilliseconds();
        if (m > out.mtimeMs) {
            out.mtimeMs = m;
            out.file    = f;
        }
    }
    return out;
}

// Relative form Rekordbox stores in djmdContent.AnalysisDataPath
static std::string toAnalysisDataPath(const juce::File& abs) {
    std::string s = abs.getFullPathName().toStdString();
    auto pos = s.find("USBANLZ");
    if (pos == std::string::npos) return {};
    std::string dir = abs.getParentDirectory().getFullPathName().toStdString();
    auto dpos = dir.find("USBANLZ");
    if (dpos == std::string::npos) return {};
    std::string rel = "/PIONEER/" + dir.substr(dpos);
    // Rekordbox stores POSIX-style slashes
    for (auto& c : rel) if (c == '\\') c = '/';
    rel += "/";
    return rel;
}

} // namespace

RekordboxAnlzWatcher::RekordboxAnlzWatcher() = default;
RekordboxAnlzWatcher::~RekordboxAnlzWatcher() { stop(); }

bool RekordboxAnlzWatcher::start() {
    if (running_.exchange(true)) return true;
    worker_ = std::thread([this] { threadLoop(); });
    spdlog::info("[RB-ANLZ] watcher started");
    return true;
}

void RekordboxAnlzWatcher::stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
}

std::optional<PlayedTrack> RekordboxAnlzWatcher::readNowPlaying() {
    std::lock_guard<std::mutex> lk(mutex_);
    auto out = pending_;
    pending_.reset();
    return out;
}

void RekordboxAnlzWatcher::threadLoop() {
    juce::File root = anlzRoot();
    if (!root.isDirectory()) {
        spdlog::warn("[RB-ANLZ] USBANLZ directory not found at expected path");
        return;
    }
    spdlog::info("[RB-ANLZ] watching {}", root.getFullPathName().toStdString());

    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(750));
        if (!running_.load()) break;

        auto hit = findNewestAnlz(root);
        if (hit.mtimeMs == 0 || hit.mtimeMs == lastSeenMtimeMs_) continue;
        lastSeenMtimeMs_ = hit.mtimeMs;
        active_.store(true);

        int64_t age = juce::Time::getCurrentTime().toMilliseconds() - hit.mtimeMs;
        if (age > 10000) continue;

        std::string relPath = toAnalysisDataPath(hit.file);
        if (relPath.empty()) continue;
        spdlog::info("[RB-ANLZ] touched {} → rel='{}'",
                     hit.file.getFullPathName().toStdString(), relPath);

        if (relPath == lastEmittedPath_) continue;

        std::string dbPath = masterDbPath();
        if (dbPath.empty()) continue;
        std::string snap = snapshotMasterDb(dbPath);
        sqlite3* db = nullptr;
        if (RekordboxCipher::openEncrypted(snap, kRekordboxMasterKey, &db) != SQLITE_OK || !db) {
            if (db) sqlite3_close(db);
            continue;
        }
        sqlite3_exec(db, "PRAGMA wal_checkpoint(TRUNCATE);",
                     nullptr, nullptr, nullptr);

        // Tolerant match patterns: Rekordbox's stored AnalysisDataPath format varies
        std::string variant1 = relPath;                                // /PIONEER/USBANLZ/.../
        std::string variant2 = relPath.substr(1);                      // PIONEER/USBANLZ/.../
        std::string variant3 = relPath;
        if (!variant3.empty() && variant3.back() == '/') variant3.pop_back();
        size_t lastSlash = variant3.find_last_of('/');
        std::string lastSeg = (lastSlash != std::string::npos)
            ? variant3.substr(lastSlash + 1) : variant3;
        std::string likeSuffix = "%" + lastSeg + "%";

        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db,
                "SELECT c.Title, a.Name, c.BPM, c.FolderPath, c.FileNameL, c.Length, c.Rating "
                "FROM djmdContent c "
                "LEFT JOIN djmdArtist a ON c.ArtistID = a.ID "
                "WHERE c.AnalysisDataPath = ? "
                "   OR c.AnalysisDataPath = ? "
                "   OR c.AnalysisDataPath LIKE ? "
                "   OR c.AnalysisDataPath LIKE ? "
                "LIMIT 1;", -1, &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, variant1.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, variant2.c_str(), -1, SQLITE_TRANSIENT);
            std::string likePat = variant1 + "%";
            sqlite3_bind_text(st, 3, likePat.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 4, likeSuffix.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(st) == SQLITE_ROW) {
                PlayedTrack pt;
                pt.title       = colStr(st, 0);
                pt.artist      = colStr(st, 1);
                pt.bpm         = sqlite3_column_double(st, 2);
                if (pt.bpm > 1000.0) pt.bpm /= 100.0;
                std::string folder = colStr(st, 3);
                std::string fname  = colStr(st, 4);
                if (!folder.empty() && !fname.empty()) {
                    if (folder.back() != '/' && folder.back() != '\\')
                        folder += '/';
                    pt.filePath = folder + fname;
                } else {
                    pt.filePath = folder;
                }
                pt.durationSec = sqlite3_column_double(st, 5);
                pt.rating      = sqlite3_column_int(st, 6);
                pt.source      = "Rekordbox";
                if (!pt.title.empty()) {
                    spdlog::info("[RB-ANLZ] resolved '{}' by '{}' ({:.2f} BPM)",
                                 pt.title, pt.artist, pt.bpm);
                    std::lock_guard<std::mutex> lk(mutex_);
                    lastEmittedPath_ = relPath;
                    pending_ = std::move(pt);
                }
            } else {
                spdlog::debug("[RB-ANLZ] no djmdContent row matches rel='{}'", relPath);
            }
            sqlite3_finalize(st);
        }
        sqlite3_close(db);
    }
    spdlog::info("[RB-ANLZ] watcher stopped");
}

} // namespace BeatMate::Services::Rekordbox
