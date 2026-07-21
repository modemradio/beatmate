#include "EngineDJService.h"

#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <filesystem>
#include <cstdlib>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#endif

namespace fs = std::filesystem;

namespace BeatMate::Services::EngineDJ {

static std::string getColumnText(sqlite3_stmt* stmt, int col) {
    const unsigned char* text = sqlite3_column_text(stmt, col);
    return text ? reinterpret_cast<const char*>(text) : "";
}

bool EngineDJService::initialize() {
    dbPath_ = findDatabasePath();
    if (dbPath_.empty()) {
        spdlog::warn("EngineDJService: Engine DJ database not found");
        return false;
    }
    initialized_ = true;
    spdlog::info("EngineDJService: Initialized with {}", dbPath_);
    return true;
}

bool EngineDJService::isAvailable() const {
    return initialized_;
}

std::vector<Models::Track> EngineDJService::readDatabase() {
    std::vector<Models::Track> tracks;
    if (!initialized_) return tracks;

    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(dbPath_.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("EngineDJService: Failed to open database: {}", sqlite3_errmsg(db));
        sqlite3_close(db);
        return tracks;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, path, filename, title, artist, album, genre, "
                      "bpmAnalyzed, tonality, year, duration, bitrate, rating "
                      "FROM Track ORDER BY title";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("EngineDJService: Query failed: {}", sqlite3_errmsg(db));
        sqlite3_close(db);
        return tracks;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Models::Track track;
        track.id = sqlite3_column_int64(stmt, 0);
        track.filePath = getColumnText(stmt, 1) + "/" + getColumnText(stmt, 2);
        track.title = getColumnText(stmt, 3);
        track.artist = getColumnText(stmt, 4);
        track.album = getColumnText(stmt, 5);
        track.genre = getColumnText(stmt, 6);
        track.bpm = sqlite3_column_double(stmt, 7);
        track.key = getColumnText(stmt, 8);
        track.year = sqlite3_column_int(stmt, 9);
        track.duration = sqlite3_column_double(stmt, 10);
        track.bitRate = sqlite3_column_int(stmt, 11);
        track.rating = sqlite3_column_int(stmt, 12);
        track.source = Models::TrackSource::EngineDJ;
        tracks.push_back(track);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    spdlog::info("EngineDJService: Read {} tracks", tracks.size());
    return tracks;
}

std::string EngineDJService::findDatabasePath() const {
#ifdef _WIN32
    std::vector<std::string> paths;
    char appData[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appData) == S_OK) {
        std::string ad(appData);
        paths.push_back(ad + "/Engine DJ/Database2/m.db");
        paths.push_back(ad + "/Engine DJ/m.db");
        paths.push_back(ad + "/Denon DJ/Engine Library/m.db");
    }
    char profile[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, profile) == S_OK) {
        std::string up(profile);
        for (const char* sub : { "/Music/Engine Library/Database2/m.db",
                                  "/Documents/Engine Library/Database2/m.db",
                                  "/Engine Library/Database2/m.db" }) {
            paths.push_back(up + sub);
        }
    }
    DWORD drives = ::GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(drives & (1u << i))) continue;
        char letter = static_cast<char>('A' + i);
        std::string root = std::string(1, letter) + ":/Engine Library/Database2/m.db";
        paths.push_back(root);
    }
    for (const auto& p : paths) {
        std::error_code ec;
        if (fs::exists(p, ec)) {
            spdlog::info("EngineDJService: m.db trouve a {}", p);
            return p;
        }
    }
    spdlog::warn("EngineDJService: m.db introuvable (essais = {})", paths.size());
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        std::string h(home);
        std::vector<std::string> paths = {
            h + "/Music/Engine Library/Database2/m.db",
            h + "/Music/Engine Library/m.db",
            h + "/Music/Engine DJ/Database2/m.db",
        };
        for (const auto& p : paths) {
            std::error_code ec;
            if (fs::exists(p, ec)) {
                spdlog::info("EngineDJService: m.db trouve a {}", p);
                return p;
            }
        }
    }
#endif
    return "";
}

std::vector<EngineDJPlaylistInfo> EngineDJService::readPlaylists() {
    std::vector<EngineDJPlaylistInfo> out;
    if (!initialized_) return out;

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath_.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        spdlog::error("EngineDJService::readPlaylists: Failed to open db: {}", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return out;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT id, title, parentListId FROM Playlist ORDER BY id",
        -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::warn("EngineDJService::readPlaylists: Playlist table query failed (maybe older schema): {}",
                     sqlite3_errmsg(db));
        sqlite3_close(db);
        return out;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        EngineDJPlaylistInfo info;
        info.engineId       = sqlite3_column_int64(stmt, 0);
        info.name           = getColumnText(stmt, 1);
        info.parentEngineId = sqlite3_column_int64(stmt, 2);
        out.push_back(std::move(info));
    }
    sqlite3_finalize(stmt);

    for (auto& pl : out) {
        sqlite3_stmt* s2 = nullptr;
        if (sqlite3_prepare_v2(db,
            "SELECT t.path, t.filename FROM PlaylistEntity pe "
            "JOIN Track t ON t.id = pe.trackId "
            "WHERE pe.listId = ? "
            "ORDER BY pe.trackNumber",
            -1, &s2, nullptr) != SQLITE_OK) {
            continue;
        }
        sqlite3_bind_int64(s2, 1, pl.engineId);
        while (sqlite3_step(s2) == SQLITE_ROW) {
            std::string full = getColumnText(s2, 0);
            std::string file = getColumnText(s2, 1);
            if (!full.empty() && !file.empty()) pl.trackPaths.push_back(full + "/" + file);
            else if (!full.empty()) pl.trackPaths.push_back(full);
        }
        sqlite3_finalize(s2);
    }

    sqlite3_close(db);
    spdlog::info("EngineDJService: Read {} playlists", out.size());
    return out;
}

}
