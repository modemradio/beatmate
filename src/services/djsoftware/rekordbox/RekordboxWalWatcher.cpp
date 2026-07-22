#include "RekordboxWalWatcher.h"
#include "RekordboxCipher.h"

#include <juce_core/juce_core.h>
#include <spdlog/spdlog.h>
#include <sqlite3.h>

#include <chrono>
#include <string>
#include <thread>

namespace BeatMate::Services::Rekordbox {

using BeatMate::Services::DJSoftware::PlayedTrack;

namespace {

// Same constant as RekordboxHistoryReader.cpp — pyrekordbox master key.
static constexpr const char* kRekordboxMasterKey =
    "402fd482c38817c35ffa8ffb8c7d93143b749e7d315df7a81732a1ff43608497";

static std::string masterDbPath()
{
#ifdef _WIN32
    auto appData = juce::File::getSpecialLocation(juce::File::windowsLocalAppData)
                       .getParentDirectory()
                       .getChildFile("Roaming");
#else
    auto appData = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                       .getChildFile("Library");
#endif
    juce::File base = appData.getChildFile("Pioneer");
    if (!base.isDirectory()) {
        base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("Pioneer");
    }
    auto candidate = base.getChildFile("rekordbox6").getChildFile("master.db");
    if (!candidate.existsAsFile()) {
        candidate = base.getChildFile("rekordbox").getChildFile("master.db");
    }
    return candidate.getFullPathName().toStdString();
}

// Snapshot db + WAL + SHM to temp so Rekordbox's open WAL is visible.
static std::string snapshotToTemp(const std::string& srcPath)
{
    juce::File src(srcPath);
    if (!src.existsAsFile()) return srcPath;
    juce::File dst = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("beatmate_wal_watcher.db");
    dst.deleteFile();
    src.copyFileTo(dst);
    juce::File walSrc(srcPath + "-wal");
    juce::File shmSrc(srcPath + "-shm");
    if (walSrc.existsAsFile()) {
        juce::File walDst(dst.getFullPathName() + "-wal");
        walDst.deleteFile();
        walSrc.copyFileTo(walDst);
    }
    if (shmSrc.existsAsFile()) {
        juce::File shmDst(dst.getFullPathName() + "-shm");
        shmDst.deleteFile();
        shmSrc.copyFileTo(shmDst);
    }
    return dst.getFullPathName().toStdString();
}

static std::string colStr(sqlite3_stmt* st, int i) {
    const unsigned char* t = sqlite3_column_text(st, i);
    return t ? reinterpret_cast<const char*>(t) : std::string{};
}

static double normaliseBpm(double raw) {
    return (raw > 1000.0) ? raw / 100.0 : raw;
}

static bool walChanged(const std::string& walPath,
                       int64_t& cachedSize, int64_t& cachedMtimeMs)
{
    juce::File f(walPath);
    if (!f.existsAsFile()) return false;
    int64_t size = f.getSize();
    int64_t mtime = f.getLastModificationTime().toMilliseconds();
    if (size == cachedSize && mtime == cachedMtimeMs) return false;
    cachedSize = size;
    cachedMtimeMs = mtime;
    return true;
}

// Rekordbox stores created_at as "YYYY-MM-DD HH:MM:SS.fff +TZ". juce's
static int64_t parseCreatedAtUnix(const std::string& s)
{
    if (s.empty()) return 0;
    std::string iso = s;
    if (iso.size() > 10 && iso[10] == ' ') iso[10] = 'T';
    // Strip fractional seconds + trailing " +HHMM" timezone if present — juce
    auto plus = iso.find_first_of("+-", 19);  // after "YYYY-MM-DDTHH:MM:SS"
    if (plus != std::string::npos && plus > 10) iso.resize(plus);
    auto dot = iso.find('.');
    if (dot != std::string::npos) iso.resize(dot);
    juce::Time t = juce::Time::fromISO8601(juce::String(iso));
    return (t.toMilliseconds() > 0) ? t.toMilliseconds() / 1000 : 0;
}

static bool queryNewestSince(sqlite3* db,
                             const std::string& sinceCreatedAt,
                             std::optional<PlayedTrack>& outTrack,
                             std::string& outCreatedAt)
{
    auto tryPrepare = [&](const char* sql) -> sqlite3_stmt* {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) == SQLITE_OK) return st;
        if (st) sqlite3_finalize(st);
        return nullptr;
    };

    const char* sqlWithDel =
        "SELECT c.FolderPath, c.Title, a.Name, c.BPM, c.Rating, c.Length, "
        "       sh.created_at "
        "FROM djmdSongHistory sh "
        "LEFT JOIN djmdContent c ON sh.ContentID = c.ID "
        "LEFT JOIN djmdArtist  a ON c.ArtistID  = a.ID "
        "WHERE sh.rb_local_deleted = 0 AND sh.created_at > ? "
        "ORDER BY sh.created_at DESC LIMIT 1;";
    const char* sqlNoDel =
        "SELECT c.FolderPath, c.Title, a.Name, c.BPM, c.Rating, c.Length, "
        "       sh.created_at "
        "FROM djmdSongHistory sh "
        "LEFT JOIN djmdContent c ON sh.ContentID = c.ID "
        "LEFT JOIN djmdArtist  a ON c.ArtistID  = a.ID "
        "WHERE sh.created_at > ? "
        "ORDER BY sh.created_at DESC LIMIT 1;";

    sqlite3_stmt* st = tryPrepare(sqlWithDel);
    if (!st) st = tryPrepare(sqlNoDel);
    if (!st) {
        spdlog::warn("[RB-WAL] prepare djmdSongHistory failed: {}", sqlite3_errmsg(db));
        return false;
    }
    sqlite3_bind_text(st, 1, sinceCreatedAt.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        PlayedTrack pt;
        pt.filePath    = colStr(st, 0);
        pt.title       = colStr(st, 1);
        pt.artist      = colStr(st, 2);
        pt.bpm         = normaliseBpm(sqlite3_column_double(st, 3));
        pt.rating      = sqlite3_column_int(st, 4);
        pt.durationSec = sqlite3_column_double(st, 5);
        outCreatedAt   = colStr(st, 6);
        pt.playedAtUnix = parseCreatedAtUnix(outCreatedAt);
        pt.source = "Rekordbox";
        outTrack = std::move(pt);
        found = true;
    }
    sqlite3_finalize(st);
    return found;
}

// Prime lastSeenCreatedAt_ with the current max created_at so we don't emit
static std::string primeMaxCreatedAt(sqlite3* db)
{
    const char* sql = "SELECT MAX(created_at) FROM djmdSongHistory;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) return {};
    std::string v;
    if (sqlite3_step(st) == SQLITE_ROW) v = colStr(st, 0);
    sqlite3_finalize(st);
    return v;
}

} // namespace

RekordboxWalWatcher::RekordboxWalWatcher() = default;
RekordboxWalWatcher::~RekordboxWalWatcher() { stop(); }

bool RekordboxWalWatcher::start()
{
    if (running_.exchange(true)) return true;
    worker_ = std::thread([this] { threadLoop(); });
    spdlog::info("[RB-WAL] watcher started");
    return true;
}

void RekordboxWalWatcher::stop()
{
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
    spdlog::info("[RB-WAL] watcher stopped");
}

std::optional<PlayedTrack> RekordboxWalWatcher::readNowPlaying()
{
    std::lock_guard<std::mutex> lk(mutex_);
    auto out = pending_;
    pending_.reset();
    return out;
}

void RekordboxWalWatcher::threadLoop()
{
    const std::string dbPath  = masterDbPath();
    const std::string walPath = dbPath + "-wal";
    spdlog::info("[RB-WAL] watching {}", walPath);

    bool primed = false;

    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        if (!running_.load()) break;

        bool changed = walChanged(walPath, lastWalSize_, lastWalMtimeMs_);
        if (!changed && primed) continue;
        if (changed) {
            spdlog::debug("[RB-WAL] WAL changed size={} mtime={}",
                          lastWalSize_, lastWalMtimeMs_);
            // Debounce: let Rekordbox finish flushing. 150ms is enough on
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }

        const std::string snap = snapshotToTemp(dbPath);
        sqlite3* db = nullptr;
        int rc = RekordboxCipher::openEncrypted(snap, kRekordboxMasterKey, &db);
        if (rc != SQLITE_OK || !db) {
            spdlog::debug("[RB-WAL] openEncrypted rc={}, skipping tick", rc);
            if (db) sqlite3_close(db);
            continue;
        }
        active_.store(true);

        // Sonde A: force WAL checkpoint to surface uncommitted page writes.
        {
            char* errmsg = nullptr;
            int crc = sqlite3_exec(db, "PRAGMA wal_checkpoint(TRUNCATE);",
                                   nullptr, nullptr, &errmsg);
            if (crc != SQLITE_OK) {
                spdlog::debug("[RB-WAL] wal_checkpoint failed: {}",
                              errmsg ? errmsg : "?");
                if (errmsg) sqlite3_free(errmsg);
            }
        }

        if (!primed) {
            lastSeenCreatedAt_ = primeMaxCreatedAt(db);
            primed = true;
            spdlog::info("[RB-WAL] primed lastSeenCreatedAt='{}'", lastSeenCreatedAt_);

            {
                const char* candidates[] = {
                    "C:\\Program Files\\Pioneer\\rekordbox\\rekordbox.exe",
                    "C:\\Program Files\\rekordbox\\rekordbox.exe",
                    "C:\\Program Files\\AlphaTheta\\rekordbox\\rekordbox.exe"
                };
                std::string rbVersion = "unknown";
                for (auto* p : candidates) {
                    juce::File f(p);
                    if (!f.existsAsFile()) {
                        // Try versioned subdirs like "rekordbox 7.1.5".
                        auto parent = f.getParentDirectory().getParentDirectory();
                        if (parent.isDirectory()) {
                            auto dirs = parent.findChildFiles(
                                juce::File::findDirectories, false, "rekordbox*");
                            for (auto& d : dirs) {
                                auto exe = d.getChildFile("rekordbox.exe");
                                if (exe.existsAsFile()) { f = exe; break; }
                            }
                        }
                    }
                    if (f.existsAsFile()) {
                        auto name = f.getParentDirectory().getFileName();
                        rbVersion = name.toStdString();
                        break;
                    }
                }
                spdlog::info("[RB-WAL] Rekordbox install detected: {}", rbVersion);
            }

            // One-shot schema dump: list columns of every realtime-relevant
            for (const char* tbl : {"djmdContent", "djmdSongHistory",
                                    "djmdSongCue", "djmdActiveCensor",
                                    "djmdSmartCue", "agentRegistry"}) {
                std::string q = "PRAGMA table_info(" + std::string(tbl) + ");";
                sqlite3_stmt* schemaSt = nullptr;
                if (sqlite3_prepare_v2(db, q.c_str(), -1, &schemaSt, nullptr) == SQLITE_OK) {
                    std::string cols;
                    while (sqlite3_step(schemaSt) == SQLITE_ROW) {
                        const unsigned char* n = sqlite3_column_text(schemaSt, 1);
                        if (!cols.empty()) cols += ", ";
                        if (n) cols += reinterpret_cast<const char*>(n);
                    }
                    sqlite3_finalize(schemaSt);
                    if (!cols.empty()) {
                        spdlog::info("[RB-WAL] {} columns: {}", tbl, cols);
                    } else {
                        spdlog::info("[RB-WAL] {}: (table absent)", tbl);
                    }
                }
            }
            // Probe: is there a usable "realtime" field? Look for StockDate,
            for (const char* probe : {"StockDate", "DJPlayCount",
                                      "contentModifiedAt", "updated_at"}) {
                std::string q = "SELECT " + std::string(probe) +
                                " FROM djmdContent ORDER BY " +
                                std::string(probe) + " DESC LIMIT 1;";
                sqlite3_stmt* st = nullptr;
                if (sqlite3_prepare_v2(db, q.c_str(), -1, &st, nullptr) == SQLITE_OK) {
                    if (sqlite3_step(st) == SQLITE_ROW) {
                        std::string v = colStr(st, 0);
                        spdlog::info("[RB-WAL] probe MAX({})='{}'", probe, v);
                    }
                    sqlite3_finalize(st);
                }
            }
            sqlite3_close(db);
            continue;
        }

        // Sonde B: scan all tables for fresh updated_at, and if a content-
        {
            std::string todayPrefix;
            {
                auto now = juce::Time::getCurrentTime();
                todayPrefix = now.formatted("%Y-%m-%d %H").toStdString();
            }
            // Also keep previous-hour prefix to catch boundary crossings.
            std::string prevPrefix;
            {
                auto t = juce::Time::getCurrentTime() - juce::RelativeTime::hours(1);
                prevPrefix = t.formatted("%Y-%m-%d %H").toStdString();
            }
            auto isFresh = [&](const std::string& v) -> bool {
                if (v.empty()) return false;
                return v.find(todayPrefix) != std::string::npos ||
                       v.find(prevPrefix)  != std::string::npos;
            };

            sqlite3_stmt* st = nullptr;
            std::vector<std::string> tables;
            if (sqlite3_prepare_v2(db,
                    "SELECT name FROM sqlite_master WHERE type='table' "
                    "ORDER BY name;",
                    -1, &st, nullptr) == SQLITE_OK) {
                while (sqlite3_step(st) == SQLITE_ROW) {
                    tables.push_back(colStr(st, 0));
                }
                sqlite3_finalize(st);
            }

            std::string fresh;
            int64_t freshestContentId = 0;
            std::string freshestStamp;
            std::string freshestSource;
            for (const auto& t : tables) {
                for (const char* col : {"updated_at", "created_at"}) {
                    std::string q = "SELECT MAX(" + std::string(col) +
                                    ") FROM " + t + ";";
                    sqlite3_stmt* s2 = nullptr;
                    if (sqlite3_prepare_v2(db, q.c_str(), -1, &s2, nullptr) == SQLITE_OK) {
                        if (sqlite3_step(s2) == SQLITE_ROW) {
                            std::string v = colStr(s2, 0);
                            if (isFresh(v)) {
                                fresh += t + "." + col + "=" + v + " | ";
                                if (v > freshestStamp) {
                                    bool isContentTable = (t == "djmdContent");
                                    bool isCueTable     = (t == "djmdSongCue" || t == "djmdCue");
                                    bool isHistoryTable = (t == "djmdSongHistory");
                                    int64_t cid = 0;
                                    std::string idCol;
                                    if (isContentTable)      idCol = "ID";
                                    else if (isCueTable)     idCol = "ContentID";
                                    else if (isHistoryTable) idCol = "ContentID";
                                    if (!idCol.empty()) {
                                        std::string q2 = "SELECT " + idCol + " FROM " + t +
                                                         " WHERE " + col + "=? LIMIT 1;";
                                        sqlite3_stmt* s3 = nullptr;
                                        if (sqlite3_prepare_v2(db, q2.c_str(), -1, &s3, nullptr) == SQLITE_OK) {
                                            sqlite3_bind_text(s3, 1, v.c_str(), -1, SQLITE_TRANSIENT);
                                            if (sqlite3_step(s3) == SQLITE_ROW) {
                                                cid = sqlite3_column_int64(s3, 0);
                                            }
                                            sqlite3_finalize(s3);
                                        }
                                    }
                                    if (cid > 0) {
                                        freshestContentId = cid;
                                        freshestStamp = v;
                                        freshestSource = t;
                                    }
                                }
                            }
                        }
                        sqlite3_finalize(s2);
                    }
                }
            }
            spdlog::info("[RB-WAL] tick fresh: {}",
                         fresh.empty() ? "(no fresh tables)" : fresh);

            if (freshestContentId > 0) {
                sqlite3_stmt* st2 = nullptr;
                if (sqlite3_prepare_v2(db,
                        "SELECT c.Title, a.Name, c.BPM, c.FolderPath, c.Length "
                        "FROM djmdContent c "
                        "LEFT JOIN djmdArtist a ON c.ArtistID = a.ID "
                        "WHERE c.ID=?;", -1, &st2, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int64(st2, 1, freshestContentId);
                    if (sqlite3_step(st2) == SQLITE_ROW) {
                        PlayedTrack pt;
                        pt.source     = "Rekordbox";
                        pt.title      = colStr(st2, 0);
                        pt.artist     = colStr(st2, 1);
                        pt.bpm        = normaliseBpm(sqlite3_column_double(st2, 2));
                        pt.filePath   = colStr(st2, 3);
                        pt.durationSec = sqlite3_column_double(st2, 4);
                        std::string key = pt.title + "|" + pt.artist + "|" + freshestStamp;
                        if (!pt.title.empty() && key != lastEmittedFallback_) {
                            spdlog::info("[RB-WAL] sonde-B emit from {} '{}' by '{}' ({:.2f} BPM) stamp='{}'",
                                         freshestSource, pt.title, pt.artist, pt.bpm, freshestStamp);
                            std::lock_guard<std::mutex> lk(mutex_);
                            lastEmittedFallback_ = key;
                            pending_ = std::move(pt);
                        }
                    }
                    sqlite3_finalize(st2);
                }
            }
        }

        std::optional<PlayedTrack> hit;
        std::string newCreatedAt;
        bool found = queryNewestSince(db, lastSeenCreatedAt_, hit, newCreatedAt);

        if (found && hit.has_value()) {
            spdlog::info("[RB-WAL] new history row created_at='{}' '{}' by '{}' ({:.2f} BPM)",
                         newCreatedAt, hit->title, hit->artist, hit->bpm);
            std::lock_guard<std::mutex> lk(mutex_);
            lastSeenCreatedAt_ = newCreatedAt;
            pending_ = std::move(hit);
            sqlite3_close(db);
            continue;
        }

        // NO stale fallback: when WAL changes but no new djmdSongHistory row,
        sqlite3_close(db);
    }
}

} // namespace BeatMate::Services::Rekordbox
