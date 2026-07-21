#include "RekordboxDatabase.h"
#include "RekordboxCipher.h"

#include <spdlog/spdlog.h>
#include <filesystem>
#include <set>
#include <vector>

namespace BeatMate::Services::Rekordbox {

static std::string getColumnText(sqlite3_stmt* stmt, int col) {
    const unsigned char* text = sqlite3_column_text(stmt, col);
    return text ? reinterpret_cast<const char*>(text) : "";
}

static std::string escapeSqlLiteral(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\'') out += "''";
        else out.push_back(c);
    }
    return out;
}

RekordboxDatabase::RekordboxDatabase() {
}

RekordboxDatabase::~RekordboxDatabase() {
    close();
}

bool RekordboxDatabase::openDatabase(const std::string& path, const std::string& key) {
    namespace fs = std::filesystem;
    spdlog::info("[RBDB] openDatabase: attempt path='{}' (keyProvided={})",
                 path, !key.empty());

    if (path.empty() || !fs::exists(path)) {
        spdlog::error("[RBDB] openDatabase: file does not exist at '{}'", path);
        return false;
    }

    {
        sqlite3* tryDb = nullptr;
        int rc = sqlite3_open_v2(path.c_str(), &tryDb, SQLITE_OPEN_READONLY, nullptr);
        if (rc == SQLITE_OK && tryDb) {
            if (!key.empty()) {
                std::string pragmaKey = "PRAGMA key = '" + escapeSqlLiteral(key) + "'";
                sqlite3_exec(tryDb, pragmaKey.c_str(), nullptr, nullptr, nullptr);
            }
            sqlite3_stmt* testStmt = nullptr;
            int pr = sqlite3_prepare_v2(tryDb, "SELECT COUNT(*) FROM sqlite_master",
                                        -1, &testStmt, nullptr);
            if (pr == SQLITE_OK && sqlite3_step(testStmt) == SQLITE_ROW) {
                sqlite3_finalize(testStmt);
                db_ = tryDb;
                isOpen_ = true;
                spdlog::info("[RBDB] openDatabase: plain SQLite OPEN OK for '{}'", path);
                return true;
            }
            if (testStmt) sqlite3_finalize(testStmt);
            spdlog::info("[RBDB] openDatabase: plain open succeeded but probe failed ({}) — falling back to SQLCipher",
                         sqlite3_errmsg(tryDb));
            sqlite3_close(tryDb);
        } else {
            spdlog::info("[RBDB] openDatabase: sqlite3_open_v2 rc={} ({}) — falling back to SQLCipher",
                         rc, tryDb ? sqlite3_errmsg(tryDb) : "null");
            if (tryDb) sqlite3_close(tryDb);
        }
    }

    {
        std::vector<std::string> candidates = {
            path + ".decrypted.db",
        };
        for (const auto& cand : candidates) {
            std::error_code ec;
            if (!fs::exists(cand, ec)) continue;
            sqlite3* tryDb = nullptr;
            int rc = sqlite3_open_v2(cand.c_str(), &tryDb, SQLITE_OPEN_READONLY, nullptr);
            if (rc == SQLITE_OK && tryDb) {
                sqlite3_stmt* testStmt = nullptr;
                int pr = sqlite3_prepare_v2(tryDb, "SELECT COUNT(*) FROM sqlite_master",
                                            -1, &testStmt, nullptr);
                if (pr == SQLITE_OK && sqlite3_step(testStmt) == SQLITE_ROW) {
                    sqlite3_finalize(testStmt);
                    db_ = tryDb;
                    isOpen_ = true;
                    spdlog::info("[RBDB] openDatabase: using decrypted sidecar '{}'", cand);
                    return true;
                }
                if (testStmt) sqlite3_finalize(testStmt);
                sqlite3_close(tryDb);
            } else if (tryDb) {
                sqlite3_close(tryDb);
            }
        }
    }

    {
        sqlite3* encDb = nullptr;
        int rc = RekordboxCipher::openEncrypted(path, key, &encDb);
        if (rc == SQLITE_OK && encDb) {
            db_ = encDb;
            isOpen_ = true;
            spdlog::info("[RBDB] openDatabase: SQLCipher OPEN OK via RekordboxCipher for '{}'", path);
            return true;
        }
        spdlog::debug("[RBDB] openDatabase: RekordboxCipher::openEncrypted rc={} for '{}' (silent; caller may fallback to XML)",
                      rc, path);
    }

    spdlog::debug("RekordboxDatabase: Database not readable via SQLCipher variant ladder "
                  "(cold-start silent fallback)");
    if (db_) { sqlite3_close(db_); db_ = nullptr; }
    isOpen_ = false;
    return false;
}

void RekordboxDatabase::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
    isOpen_ = false;
}

bool RekordboxDatabase::isOpen() const {
    return isOpen_;
}

std::vector<Models::RekordboxTrack> RekordboxDatabase::readAllTracks() {
    std::vector<Models::RekordboxTrack> tracks;
    if (!isOpen_) return tracks;

    const char* sqlV7 =
        "SELECT dc.ID, dc.FolderPath, dc.Title, "
        "       da.Name, dab.Name, dg.Name, "
        "       dc.BPM, dc.ColorID, dc.Rating, dc.Commnt, "
        "       dc.Length, dc.ReleaseYear, dc.DateCreated, "
        "       dk.ScaleName "
        "FROM djmdContent dc "
        "LEFT JOIN djmdArtist da  ON dc.ArtistID = da.ID "
        "LEFT JOIN djmdAlbum  dab ON dc.AlbumID  = dab.ID "
        "LEFT JOIN djmdGenre  dg  ON dc.GenreID  = dg.ID "
        "LEFT JOIN djmdKey    dk  ON dc.KeyID    = dk.ID "
        "WHERE dc.FolderPath IS NOT NULL "
        "ORDER BY dc.Title";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sqlV7, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::warn("RekordboxDatabase::readAllTracks: V7 JOIN query failed: {} — fallback v5",
                     sqlite3_errmsg(db_));
        const char* sqlOld =
            "SELECT rb_local_content_id, file_path, title, artist, album, genre, "
            "       bpm, color, rating, comment, "
            "       duration, year, date_added, key "
            "FROM content WHERE file_path IS NOT NULL ORDER BY title";
        rc = sqlite3_prepare_v2(db_, sqlOld, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            spdlog::error("RekordboxDatabase: Failed to read tracks: {}",
                          sqlite3_errmsg(db_));
            return tracks;
        }
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Models::RekordboxTrack t;
        t.rekordboxId   = getColumnText(stmt, 0);
        t.externalId    = t.rekordboxId;
        t.externalPath  = getColumnText(stmt, 1);
        t.title         = getColumnText(stmt, 2);
        t.artist        = getColumnText(stmt, 3);
        t.album         = getColumnText(stmt, 4);
        t.genre         = getColumnText(stmt, 5);
        const double rawBpm = sqlite3_column_double(stmt, 6);
        t.bpm = (rawBpm > 1000.0) ? rawBpm / 100.0 : rawBpm;
        t.color         = getColumnText(stmt, 7);
        t.rating        = sqlite3_column_int(stmt, 8);
        t.comment       = getColumnText(stmt, 9);
        t.duration      = sqlite3_column_double(stmt, 10);
        t.year          = sqlite3_column_int(stmt, 11);
        t.dateAdded     = getColumnText(stmt, 12);
        t.tonality      = getColumnText(stmt, 13);
        t.source        = Models::TrackSource::Rekordbox;
        tracks.push_back(std::move(t));
    }

    sqlite3_finalize(stmt);
    spdlog::info("RekordboxDatabase: Read {} tracks", tracks.size());
    return tracks;
}

std::vector<Models::Playlist> RekordboxDatabase::readPlaylists() {
    std::vector<Models::Playlist> playlists;
    if (!isOpen_) return playlists;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT ID, Name, Seq, ParentID, Attribute FROM djmdPlaylist ORDER BY Seq",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("RekordboxDatabase: Failed to read playlists: {}", sqlite3_errmsg(db_));
        return playlists;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Models::Playlist pl;
        pl.externalId = getColumnText(stmt, 0);
        pl.name = getColumnText(stmt, 1);
        pl.source = Models::PlaylistSource::Rekordbox;
        playlists.push_back(pl);
    }

    sqlite3_finalize(stmt);
    return playlists;
}

std::vector<RekordboxDatabase::RekordboxPlaylistInfo>
RekordboxDatabase::readPlaylistsRich() {
    std::vector<RekordboxPlaylistInfo> out;
    if (!isOpen_) {
        spdlog::warn("[RBDB] readPlaylistsRich: db not open");
        return out;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT ID, Name, Seq, ParentID, Attribute FROM djmdPlaylist "
        "ORDER BY Seq";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("[RBDB] readPlaylistsRich: prepare failed rc={} ({})",
                      rc, sqlite3_errmsg(db_));
        return out;
    }
    int roots = 0, folders = 0, playlistsCount = 0;
    std::set<std::string> distinctParents;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        RekordboxPlaylistInfo p;
        p.externalId       = getColumnText(stmt, 0);
        p.name             = getColumnText(stmt, 1);
        p.seq              = sqlite3_column_int(stmt, 2);
        p.parentExternalId = getColumnText(stmt, 3);
        p.attribute        = sqlite3_column_int(stmt, 4);
        if (p.parentExternalId.empty() || p.parentExternalId == "0" ||
            p.parentExternalId == "root")
            ++roots;
        if (p.attribute == 1) ++folders; else ++playlistsCount;
        distinctParents.insert(p.parentExternalId);
        out.push_back(std::move(p));
    }
    sqlite3_finalize(stmt);
    spdlog::info("[RBDB] readPlaylistsRich: {} rows (roots={}, folders={}, playlists={}, "
                 "distinctParents={})",
                 out.size(), roots, folders, playlistsCount, distinctParents.size());
    int dumped = 0;
    for (const auto& p : out) {
        if (dumped >= 3) break;
        spdlog::info("[RBDB]   sample pl: id='{}' parent='{}' name='{}' attr={} seq={}",
                     p.externalId, p.parentExternalId, p.name, p.attribute, p.seq);
        ++dumped;
    }
    return out;
}

std::vector<std::string> RekordboxDatabase::readPlaylistContentIds(
    const std::string& playlistExternalId) {
    std::vector<std::string> out;
    if (!isOpen_ || playlistExternalId.empty()) return out;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT ContentID FROM djmdSongPlaylist WHERE PlaylistID = ? ORDER BY TrackNo",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("RekordboxDatabase: readPlaylistContentIds prepare failed: {}",
                      sqlite3_errmsg(db_));
        return out;
    }
    sqlite3_bind_text(stmt, 1, playlistExternalId.c_str(),
                      static_cast<int>(playlistExternalId.size()), SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.push_back(getColumnText(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::string RekordboxDatabase::readContentFilePath(const std::string& contentId) {
    if (!isOpen_ || contentId.empty()) return {};
    std::string path;
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT FolderPath FROM djmdContent WHERE ID = ? LIMIT 1",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return {};
    sqlite3_bind_text(stmt, 1, contentId.c_str(),
                      static_cast<int>(contentId.size()), SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        path = getColumnText(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return path;
}

std::vector<Models::RekordboxCue> RekordboxDatabase::readCuePoints(const std::string& contentId) {
    std::vector<Models::RekordboxCue> cues;
    if (!isOpen_) return cues;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT HotCueNum, InMsec, OutMsec, ColorID FROM djmdCue WHERE ContentID = ? ORDER BY HotCueNum",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("RekordboxDatabase: Failed to read cue points: {}", sqlite3_errmsg(db_));
        return cues;
    }

    sqlite3_bind_text(stmt, 1, contentId.c_str(), static_cast<int>(contentId.size()), SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Models::RekordboxCue cue;
        cue.number = sqlite3_column_int(stmt, 0);
        cue.position = sqlite3_column_double(stmt, 1) / 1000.0;
        double outMs = sqlite3_column_double(stmt, 2);
        if (outMs > 0) {
            cue.length = (outMs / 1000.0) - cue.position;
            cue.isLoop = true;
        }
        cues.push_back(cue);
    }

    sqlite3_finalize(stmt);
    return cues;
}

Models::RekordboxTrack RekordboxDatabase::trackFromStatement(sqlite3_stmt* stmt, bool isV6Schema) {
    Models::RekordboxTrack track;

    if (isV6Schema) {
        track.rekordboxId = getColumnText(stmt, 0);
        track.externalId = track.rekordboxId;
        track.externalPath = getColumnText(stmt, 1);
    } else {
        track.externalId = getColumnText(stmt, 0);
        track.rekordboxId = track.externalId;
        track.externalPath = getColumnText(stmt, 1);
    }

    track.source = Models::TrackSource::Rekordbox;
    return track;
}

}
