#include "RekordboxPlaylistsXmlWatcher.h"
#include "RekordboxCipher.h"

#include <juce_core/juce_core.h>
#include <spdlog/spdlog.h>
#include <sqlite3.h>

#include <chrono>
#include <regex>
#include <thread>

namespace BeatMate::Services::Rekordbox {

using BeatMate::Services::DJSoftware::PlayedTrack;

namespace {

static std::string masterDbPath() {
#ifdef _WIN32
    auto appData = juce::File::getSpecialLocation(juce::File::windowsLocalAppData)
                       .getParentDirectory().getChildFile("Roaming");
#else
    auto appData = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                       .getChildFile("Library");
#endif
    juce::File base = appData.getChildFile("Pioneer");
    if (!base.isDirectory()) {
        base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("Pioneer");
    }
    auto rb = base.getChildFile("rekordbox");
    if (rb.isDirectory()) {
        auto db = rb.getChildFile("master.db");
        if (db.existsAsFile()) return db.getFullPathName().toStdString();
    }
    auto rb6 = base.getChildFile("rekordbox6").getChildFile("master.db");
    if (rb6.existsAsFile()) return rb6.getFullPathName().toStdString();
    return {};
}

static std::string playlistsXmlPath() {
    auto db = masterDbPath();
    if (db.empty()) return {};
    return juce::File(db).getSiblingFile("masterPlaylists6.xml")
        .getFullPathName().toStdString();
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
                         .getChildFile("beatmate_pl_snapshot.db");
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

struct PlaylistTouch {
    std::string id;
    int64_t     timestampMs = 0;
};
static PlaylistTouch parseLatestNode(const std::string& xmlPath) {
    PlaylistTouch out;
    juce::File f(xmlPath);
    if (!f.existsAsFile()) return out;
    juce::String body = f.loadFileAsString();
    std::string s = body.toStdString();
    static const std::regex re(R"(<NODE\s+Id=\"([^\"]+)\"[^>]*Timestamp=\"(\d+)\")");
    auto begin = std::sregex_iterator(s.begin(), s.end(), re);
    auto end   = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        const auto& m = *it;
        int64_t ts = 0;
        try { ts = std::stoll(m[2].str()); } catch (...) {}
        if (ts > out.timestampMs) {
            out.timestampMs = ts;
            out.id          = m[1].str();
        }
    }
    return out;
}

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

RekordboxPlaylistsXmlWatcher::RekordboxPlaylistsXmlWatcher() = default;
RekordboxPlaylistsXmlWatcher::~RekordboxPlaylistsXmlWatcher() { stop(); }

bool RekordboxPlaylistsXmlWatcher::start() {
    if (running_.exchange(true)) return true;
    worker_ = std::thread([this] { threadLoop(); });
    spdlog::info("[RB-Playlists] watcher started");
    return true;
}

void RekordboxPlaylistsXmlWatcher::stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
}

std::optional<PlayedTrack> RekordboxPlaylistsXmlWatcher::readNowPlaying() {
    std::lock_guard<std::mutex> lk(mutex_);
    auto out = pending_;
    pending_.reset();
    return out;
}

void RekordboxPlaylistsXmlWatcher::threadLoop() {
    const std::string xml = playlistsXmlPath();
    if (xml.empty()) {
        spdlog::warn("[RB-Playlists] could not resolve masterPlaylists6.xml path");
        return;
    }
    spdlog::info("[RB-Playlists] watching {}", xml);

    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (!running_.load()) break;

        juce::File f(xml);
        if (!f.existsAsFile()) continue;
        int64_t mtime = f.getLastModificationTime().toMilliseconds();
        if (mtime == lastMtimeMs_) continue;
        lastMtimeMs_ = mtime;
        active_.store(true);

        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        auto touch = parseLatestNode(xml);
        if (touch.id.empty() || touch.id == lastEmittedNodeId_) continue;
        spdlog::info("[RB-Playlists] mtime change → playlist Id={} ts={}",
                     touch.id, touch.timestampMs);

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

        std::string sql =
            "SELECT c.Title, a.Name, c.BPM, c.FolderPath, c.FileNameL, c.Length, sp.created_at "
            "FROM djmdSongPlaylist sp "
            "JOIN djmdPlaylist p ON sp.PlaylistID = p.ID "
            "JOIN djmdContent  c ON sp.ContentID  = c.ID "
            "LEFT JOIN djmdArtist a ON c.ArtistID = a.ID "
            "WHERE p.ID = ? "
            "ORDER BY sp.created_at DESC LIMIT 1;";
        const char* sqlFallback =
            "SELECT c.Title, a.Name, c.BPM, c.FolderPath, c.FileNameL, c.Length, sp.created_at "
            "FROM djmdSongPlaylist sp "
            "JOIN djmdContent c ON sp.ContentID = c.ID "
            "LEFT JOIN djmdArtist a ON c.ArtistID = a.ID "
            "ORDER BY sp.created_at DESC LIMIT 1;";

        sqlite3_stmt* st = nullptr;
        bool stepOk = false;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, touch.id.c_str(), -1, SQLITE_TRANSIENT);
            stepOk = (sqlite3_step(st) == SQLITE_ROW);
            if (!stepOk) {
                sqlite3_finalize(st);
                if (sqlite3_prepare_v2(db, sqlFallback, -1, &st, nullptr) == SQLITE_OK) {
                    stepOk = (sqlite3_step(st) == SQLITE_ROW);
                }
            }
            if (stepOk) {
                std::string createdAt = colStr(st, 6);
                bool fresh = false;
                if (!createdAt.empty()) {
                    std::string norm = createdAt;
                    if (norm.size() > 10 && norm[10] == ' ') norm[10] = 'T';
                    auto dotPos = norm.find('.');
                    if (dotPos != std::string::npos) norm.resize(dotPos);
                    juce::Time t = juce::Time::fromISO8601(juce::String(norm));
                    int64_t ageSec = (juce::Time::currentTimeMillis() - t.toMilliseconds()) / 1000;
                    fresh = (t.toMilliseconds() > 0 && ageSec >= -10 && ageSec <= 180);
                    if (!fresh) {
                        spdlog::debug("[RB-Playlists] row too old ({}s), ignoring", ageSec);
                    }
                }
                if (!fresh) {
                    sqlite3_finalize(st);
                    sqlite3_close(db);
                    continue;
                }
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
                pt.source      = "Rekordbox";
                pt.playedAtUnix = nowMs() / 1000;
                if (!pt.title.empty()) {
                    spdlog::info("[RB-Playlists] resolved track '{}' by '{}' ({:.2f} BPM) playlist={}",
                                 pt.title, pt.artist, pt.bpm, touch.id);
                    std::lock_guard<std::mutex> lk(mutex_);
                    lastEmittedNodeId_ = touch.id;
                    pending_ = std::move(pt);
                }
            }
            sqlite3_finalize(st);
        }
        sqlite3_close(db);
    }
    spdlog::info("[RB-Playlists] watcher stopped");
}

} // namespace BeatMate::Services::Rekordbox
