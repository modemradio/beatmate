#include "TrackDatabase.h"

#include <spdlog/spdlog.h>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <random>

namespace BeatMate::Services::Library {

TrackDatabase::TrackDatabase() = default;

TrackDatabase::TrackDatabase(const std::string& dbPath) : dbPath_(dbPath) {}

TrackDatabase::~TrackDatabase() {
    close();
}

bool TrackDatabase::initialize(const std::string& dbPath) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!dbPath.empty()) {
        dbPath_ = dbPath;
    }

    // Suppression AVANT ouverture : un WAL/SHM oublie corrompt la base.
    {
        std::string walPath = dbPath_ + "-wal";
        std::string shmPath = dbPath_ + "-shm";
        if (std::filesystem::exists(walPath)) {
            std::filesystem::remove(walPath);
            spdlog::info("TrackDatabase: Removed stale WAL file");
        }
        if (std::filesystem::exists(shmPath)) {
            std::filesystem::remove(shmPath);
            spdlog::info("TrackDatabase: Removed stale SHM file");
        }
    }

    int rc = sqlite3_open(dbPath_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        spdlog::error("TrackDatabase: Failed to open database at {}: {}", dbPath_, sqlite3_errmsg(db_));
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    sqlite3_busy_timeout(db_, 5000);

    // Mode DELETE impose : le WAL corrompt la base sur certaines machines.
    executeSQL("PRAGMA journal_mode=DELETE");
    executeSQL("PRAGMA synchronous=FULL");
    executeSQL("PRAGMA foreign_keys=ON");
    executeSQL("PRAGMA cache_size=-8000");

    {
        sqlite3_stmt* stmt = nullptr;
        rc = sqlite3_prepare_v2(db_, "PRAGMA integrity_check", -1, &stmt, nullptr);
        if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
            std::string result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (result != "ok") {
                spdlog::error("TrackDatabase: INTEGRITY CHECK FAILED: {}", result);
                sqlite3_finalize(stmt);
                executeSQL("REINDEX");
                spdlog::info("TrackDatabase: REINDEX attempted to repair");
            } else {
                spdlog::info("TrackDatabase: Integrity check: OK");
            }
        }
        if (stmt) sqlite3_finalize(stmt);
    }

    if (!createTables()) {
        spdlog::error("TrackDatabase: Failed to create tables");
        return false;
    }

    if (!createIndexes()) {
        spdlog::error("TrackDatabase: Failed to create indexes");
        return false;
    }

    if (!setupFTS5()) {
        spdlog::warn("TrackDatabase: FTS5 setup failed, full-text search may not be available");
    }

    isOpen_ = true;
    spdlog::info("TrackDatabase: Initialized at {}", dbPath_);
    return true;
}

bool TrackDatabase::migrate() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    int version = getCurrentSchemaVersion();
    spdlog::info("TrackDatabase: Current schema version: {}, target: {}", version, CURRENT_SCHEMA_VERSION);

    if (version >= CURRENT_SCHEMA_VERSION) {
        return true;
    }

    if (!beginTransaction()) return false;

    auto addColumnIfMissing = [this](const char* table, const char* col, const char* type, const char* def) -> bool {
        std::string sql = std::string("ALTER TABLE ") + table + " ADD COLUMN " + col + " " + type;
        if (def) sql += std::string(" DEFAULT ") + def;
        char* err = nullptr;
        int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            const std::string msg = err ? err : "(null)";
            sqlite3_free(err);
            // "duplicate column name" = colonne deja migree, benin.
            if (msg.find("duplicate column") == std::string::npos) {
                spdlog::error("TrackDatabase: ALTER {} ADD {} failed: {}", table, col, msg);
                return false;
            }
        } else if (err) {
            sqlite3_free(err);
        }
        return true;
    };

    auto addTrackCol    = [&](const char* c, const char* t, const char* d) {
        return addColumnIfMissing("tracks", c, t, d);
    };
    auto addPlaylistCol = [&](const char* c, const char* t, const char* d) {
        return addColumnIfMissing("playlists", c, t, d);
    };

    bool ok = true;

    ok = ok && addTrackCol("date_added", "INTEGER", "0");
    ok = ok && addTrackCol("last_modified", "INTEGER", "0");
    ok = ok && addTrackCol("file_path", "TEXT", "''");
    ok = ok && addTrackCol("analyzed", "INTEGER", "0");
    ok = ok && addTrackCol("analyzed_date", "INTEGER", "0");
    ok = ok && addTrackCol("last_played", "INTEGER", "0");
    ok = ok && addTrackCol("play_count", "INTEGER", "0");
    ok = ok && addTrackCol("file_size", "INTEGER", "0");
    ok = ok && addTrackCol("file_format", "TEXT", "''");
    ok = ok && addTrackCol("camelot_key", "TEXT", "''");
    ok = ok && addTrackCol("open_key", "TEXT", "''");
    ok = ok && addTrackCol("mood", "TEXT", "''");
    ok = ok && addTrackCol("danceability", "REAL", "0.0");
    ok = ok && addTrackCol("color", "TEXT", "''");
    ok = ok && addTrackCol("label", "TEXT", "''");
    ok = ok && addTrackCol("grouping", "TEXT", "''");
    ok = ok && addTrackCol("source", "INTEGER", "0");
    ok = ok && addTrackCol("comment", "TEXT", "''");
    ok = ok && addTrackCol("album", "TEXT", "''");
    ok = ok && addTrackCol("year", "INTEGER", "0");
    ok = ok && addTrackCol("sample_rate", "INTEGER", "0");
    ok = ok && addTrackCol("channels", "INTEGER", "0");
    ok = ok && addTrackCol("bit_rate", "INTEGER", "0");
    ok = ok && addTrackCol("bit_depth", "INTEGER", "0");
    ok = ok && addTrackCol("duration", "REAL", "0.0");
    ok = ok && addTrackCol("bpm", "REAL", "0.0");
    ok = ok && addTrackCol("key", "TEXT", "''");
    ok = ok && addTrackCol("energy", "REAL", "0.0");
    ok = ok && addTrackCol("rating", "INTEGER", "0");
    ok = ok && addTrackCol("title", "TEXT", "''");
    ok = ok && addTrackCol("artist", "TEXT", "''");
    ok = ok && addTrackCol("genre", "TEXT", "''");

    // Schema v6.
    ok = ok && addTrackCol("intro_start", "REAL", "-1.0");
    ok = ok && addTrackCol("intro_end",   "REAL", "-1.0");
    ok = ok && addTrackCol("outro_start", "REAL", "-1.0");
    ok = ok && addTrackCol("outro_end",   "REAL", "-1.0");
    ok = ok && addTrackCol("role",  "TEXT", "''");
    ok = ok && addTrackCol("venue", "TEXT", "''");

    // Schema v8.
    ok = ok && addTrackCol("lufs", "REAL", "0.0");
    ok = ok && addTrackCol("energy_segments", "TEXT", "''");

    ok = ok && addTrackCol("bpm_confidence", "REAL", "0.0");
    ok = ok && addTrackCol("key_confidence", "REAL", "0.0");
    ok = ok && addTrackCol("true_peak", "REAL", "-100.0");
    ok = ok && addTrackCol("loudness_range", "REAL", "0.0");
    ok = ok && addTrackCol("sections", "TEXT", "''");

    // Schema v10 : provenance des bpm/cle/energie importes d'un logiciel DJ.
    ok = ok && addTrackCol("analysis_source", "TEXT", "''");

    if (ok) spdlog::info("TrackDatabase: Schema columns verified/added");

    ok = ok && addPlaylistCol("is_smart", "INTEGER", "0");
    ok = ok && addPlaylistCol("created_at", "INTEGER", "0");
    ok = ok && addPlaylistCol("modified_at", "INTEGER", "0");
    ok = ok && addPlaylistCol("color", "TEXT", "''");
    ok = ok && addPlaylistCol("icon", "TEXT", "''");
    ok = ok && addPlaylistCol("parent_folder_id", "INTEGER", "-1");
    ok = ok && addPlaylistCol("sort_order", "TEXT", "'Manual'");
    ok = ok && addPlaylistCol("sort_direction", "TEXT", "'Ascending'");
    ok = ok && addPlaylistCol("smart_rules", "TEXT", "''");

    if (ok && version < 7) {
        ok = ok && addPlaylistCol("source", "TEXT", "'Local'");
        ok = ok && addPlaylistCol("external_id", "TEXT", "''");
        ok = ok && addPlaylistCol("external_path", "TEXT", "''");

        char* err = nullptr;
        int rc = sqlite3_exec(db_,
            "CREATE INDEX IF NOT EXISTS idx_playlists_source ON playlists(source);",
            nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            spdlog::error("TrackDatabase: idx_playlists_source create failed: {}", err ? err : "?");
            sqlite3_free(err);
            ok = false;
        } else if (err) { sqlite3_free(err); }

        rc = sqlite3_exec(db_,
            "CREATE INDEX IF NOT EXISTS idx_playlists_external ON playlists(source, external_id);",
            nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            spdlog::error("TrackDatabase: idx_playlists_external create failed: {}", err ? err : "?");
            sqlite3_free(err);
            ok = false;
        } else if (err) { sqlite3_free(err); }
    }

    if (!ok) {
        spdlog::error("TrackDatabase: Migration failed, rolling back");
        rollbackTransaction();
        return false;
    }

    if (!setSchemaVersion(CURRENT_SCHEMA_VERSION)) {
        spdlog::error("TrackDatabase: setSchemaVersion failed");
        rollbackTransaction();
        return false;
    }

    if (!commitTransaction()) {
        spdlog::error("TrackDatabase: commit failed");
        rollbackTransaction();
        return false;
    }

    spdlog::info("TrackDatabase: Migration complete to version {}", CURRENT_SCHEMA_VERSION);
    return true;
}

void TrackDatabase::close() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
        isOpen_ = false;
    }
    spdlog::info("TrackDatabase: Closed");
}

bool TrackDatabase::isOpen() const {
    return isOpen_ && db_ != nullptr;
}

bool TrackDatabase::executeSQL(const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::string error = errMsg ? errMsg : "unknown error";
        sqlite3_free(errMsg);
        spdlog::error("TrackDatabase: SQL error: {} (query: {})", error, sql);
        return false;
    }
    return true;
}

bool TrackDatabase::executeWrite(const std::string& sql,
                                 const std::vector<std::string>& params) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return false;
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("TrackDatabase::executeWrite prepare failed: {} (sql: {})",
                      sqlite3_errmsg(db_), sql);
        return false;
    }
    for (size_t i = 0; i < params.size(); ++i) {
        sqlite3_bind_text(stmt, static_cast<int>(i + 1), params[i].c_str(),
                          static_cast<int>(params[i].size()), SQLITE_TRANSIENT);
    }
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        spdlog::error("TrackDatabase::executeWrite step failed: {} (sql: {})",
                      sqlite3_errmsg(db_), sql);
        return false;
    }
    return true;
}

std::vector<std::string> TrackDatabase::getTrackTags(int64_t trackId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::string> out;
    if (!db_) return out;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
            "SELECT t.name FROM tags t JOIN track_tags tt ON tt.tag_id = t.id "
            "WHERE tt.track_id = ? ORDER BY t.name COLLATE NOCASE",
            -1, &stmt, nullptr) != SQLITE_OK)
        return out;
    sqlite3_bind_int64(stmt, 1, trackId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* txt = sqlite3_column_text(stmt, 0);
        if (txt) out.emplace_back(reinterpret_cast<const char*>(txt));
    }
    sqlite3_finalize(stmt);
    return out;
}

bool TrackDatabase::setTrackTags(int64_t trackId, const std::vector<std::string>& tags) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return false;
    executeWrite("DELETE FROM track_tags WHERE track_id = ?", { std::to_string(trackId) });
    for (const auto& raw : tags) {
        auto tag = raw;
        while (!tag.empty() && (tag.front() == ' ' || tag.front() == '\t')) tag.erase(tag.begin());
        while (!tag.empty() && (tag.back() == ' ' || tag.back() == '\t')) tag.pop_back();
        if (tag.empty()) continue;
        executeWrite("INSERT OR IGNORE INTO tags (name) VALUES (?)", { tag });
        executeWrite("INSERT OR IGNORE INTO track_tags (track_id, tag_id) "
                     "SELECT ?, id FROM tags WHERE name = ?",
                     { std::to_string(trackId), tag });
    }
    executeSQL("DELETE FROM tags WHERE id NOT IN (SELECT DISTINCT tag_id FROM track_tags)");
    return true;
}

std::vector<std::string> TrackDatabase::getAllTags() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::string> out;
    if (!db_) return out;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT name FROM tags ORDER BY name COLLATE NOCASE",
                           -1, &stmt, nullptr) != SQLITE_OK)
        return out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* txt = sqlite3_column_text(stmt, 0);
        if (txt) out.emplace_back(reinterpret_cast<const char*>(txt));
    }
    sqlite3_finalize(stmt);
    return out;
}

int64_t TrackDatabase::getLastInsertRowId() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return db_ ? sqlite3_last_insert_rowid(db_) : 0;
}

bool TrackDatabase::executeRead(const std::string& sql,
                                const std::vector<std::string>& params,
                                const std::function<void(sqlite3_stmt*)>& rowCallback) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return false;
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("TrackDatabase::executeRead prepare failed: {} (sql: {})",
                      sqlite3_errmsg(db_), sql);
        return false;
    }
    for (size_t i = 0; i < params.size(); ++i) {
        sqlite3_bind_text(stmt, static_cast<int>(i + 1), params[i].c_str(),
                          static_cast<int>(params[i].size()), SQLITE_TRANSIENT);
    }
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (rowCallback) rowCallback(stmt);
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

void TrackDatabase::bindString(sqlite3_stmt* stmt, int index, const std::string& value) {
    sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

int TrackDatabase::getColumnIndex(sqlite3_stmt* stmt, const char* name) {
    int cols = sqlite3_column_count(stmt);
    for (int i = 0; i < cols; ++i) {
        const char* colName = sqlite3_column_name(stmt, i);
        if (colName && strcmp(colName, name) == 0) return i;
    }
    return -1;
}

bool TrackDatabase::createTables() {
    if (!executeSQL("CREATE TABLE IF NOT EXISTS schema_version ("
                    "version INTEGER NOT NULL DEFAULT 0"
                    ")")) {
        return false;
    }

    executeSQL("INSERT INTO schema_version (version) SELECT 0 WHERE NOT EXISTS (SELECT 1 FROM schema_version)");

    if (!executeSQL(
        "CREATE TABLE IF NOT EXISTS tracks ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "file_path TEXT UNIQUE NOT NULL, "
        "title TEXT, "
        "artist TEXT, "
        "album TEXT, "
        "genre TEXT, "
        "year INTEGER DEFAULT 0, "
        "comment TEXT, "
        "duration REAL DEFAULT 0.0, "
        "sample_rate INTEGER DEFAULT 44100, "
        "channels INTEGER DEFAULT 2, "
        "bit_rate INTEGER DEFAULT 320, "
        "bit_depth INTEGER DEFAULT 16, "
        "bpm REAL DEFAULT 0.0, "
        "key TEXT, "
        "energy REAL DEFAULT 0.0, "
        "rating INTEGER DEFAULT 0, "
        "play_count INTEGER DEFAULT 0, "
        "last_played INTEGER DEFAULT 0, "
        "color TEXT, "
        "label TEXT, "
        "grouping TEXT, "
        "date_added INTEGER DEFAULT 0, "
        "last_modified INTEGER DEFAULT 0, "
        "file_size INTEGER DEFAULT 0, "
        "file_format TEXT, "
        "analyzed INTEGER DEFAULT 0, "
        "analyzed_date INTEGER DEFAULT 0, "
        "camelot_key TEXT, "
        "open_key TEXT, "
        "mood TEXT, "
        "danceability REAL DEFAULT 0.0, "
        "lufs REAL DEFAULT 0.0, "
        "energy_segments TEXT DEFAULT '', "
        "bpm_confidence REAL DEFAULT 0.0, "
        "key_confidence REAL DEFAULT 0.0, "
        "true_peak REAL DEFAULT -100.0, "
        "loudness_range REAL DEFAULT 0.0, "
        "sections TEXT DEFAULT '', "
        "source INTEGER DEFAULT 0, "
        "intro_start REAL DEFAULT -1.0, "
        "intro_end REAL DEFAULT -1.0, "
        "outro_start REAL DEFAULT -1.0, "
        "outro_end REAL DEFAULT -1.0, "
        "role TEXT DEFAULT '', "
        "venue TEXT DEFAULT '', "
        "analysis_source TEXT DEFAULT ''"
        ")")) {
        return false;
    }

    if (!executeSQL(
        "CREATE TABLE IF NOT EXISTS cue_points ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "track_id INTEGER NOT NULL, "
        "type INTEGER DEFAULT 0, "
        "position REAL DEFAULT 0.0, "
        "length REAL DEFAULT 0.0, "
        "name TEXT, "
        "color TEXT, "
        "number INTEGER DEFAULT 0, "
        "FOREIGN KEY (track_id) REFERENCES tracks(id) ON DELETE CASCADE"
        ")")) {
        return false;
    }

    if (!executeSQL(
        "CREATE TABLE IF NOT EXISTS tags ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "name TEXT UNIQUE NOT NULL"
        ")")) {
        return false;
    }

    // Watermark de sync incrementale par logiciel DJ.
    if (!executeSQL(
        "CREATE TABLE IF NOT EXISTS sync_state ("
        "software TEXT PRIMARY KEY, "
        "watermark TEXT NOT NULL DEFAULT '', "
        "last_sync INTEGER NOT NULL DEFAULT 0, "
        "sync_count INTEGER NOT NULL DEFAULT 0"
        ")")) {
        return false;
    }

    if (!executeSQL(
        "CREATE TABLE IF NOT EXISTS track_embeddings ("
        "track_id INTEGER PRIMARY KEY, "
        "dim INTEGER NOT NULL, "
        "vec BLOB NOT NULL, "
        "model_version TEXT NOT NULL, "
        "file_mtime INTEGER NOT NULL DEFAULT 0, "
        "computed_at INTEGER NOT NULL DEFAULT 0"
        ")")) {
        return false;
    }

    if (!executeSQL(
        "CREATE TABLE IF NOT EXISTS track_tags ("
        "track_id INTEGER NOT NULL, "
        "tag_id INTEGER NOT NULL, "
        "PRIMARY KEY (track_id, tag_id), "
        "FOREIGN KEY (track_id) REFERENCES tracks(id) ON DELETE CASCADE, "
        "FOREIGN KEY (tag_id) REFERENCES tags(id) ON DELETE CASCADE"
        ")")) {
        return false;
    }

    if (!executeSQL(
        "CREATE TABLE IF NOT EXISTS playlists ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "name TEXT NOT NULL, "
        "description TEXT, "
        "is_smart INTEGER DEFAULT 0, "
        "created_at INTEGER DEFAULT 0, "
        "modified_at INTEGER DEFAULT 0, "
        "color TEXT, "
        "icon TEXT, "
        "parent_folder_id INTEGER DEFAULT -1, "
        "sort_order INTEGER DEFAULT 0, "
        "sort_direction INTEGER DEFAULT 0, "
        "smart_rules TEXT"
        ")")) {
        return false;
    }

    if (!executeSQL(
        "CREATE TABLE IF NOT EXISTS playlist_tracks ("
        "playlist_id INTEGER NOT NULL, "
        "track_id INTEGER NOT NULL, "
        "position INTEGER DEFAULT 0, "
        "added_at INTEGER DEFAULT 0, "
        "PRIMARY KEY (playlist_id, track_id), "
        "FOREIGN KEY (playlist_id) REFERENCES playlists(id) ON DELETE CASCADE, "
        "FOREIGN KEY (track_id) REFERENCES tracks(id) ON DELETE CASCADE"
        ")")) {
        return false;
    }

    if (!executeSQL(
        "CREATE TABLE IF NOT EXISTS play_history ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "track_id INTEGER NOT NULL, "
        "played_at INTEGER NOT NULL, "
        "context TEXT, "
        "FOREIGN KEY (track_id) REFERENCES tracks(id) ON DELETE CASCADE"
        ")")) {
        return false;
    }

    return true;
}

bool TrackDatabase::createIndexes() {
    executeSQL("CREATE INDEX IF NOT EXISTS idx_tracks_artist ON tracks(artist)");
    executeSQL("CREATE INDEX IF NOT EXISTS idx_tracks_genre ON tracks(genre)");
    executeSQL("CREATE INDEX IF NOT EXISTS idx_tracks_bpm ON tracks(bpm)");
    executeSQL("CREATE INDEX IF NOT EXISTS idx_tracks_key ON tracks(key)");
    executeSQL("CREATE INDEX IF NOT EXISTS idx_tracks_rating ON tracks(rating)");
    executeSQL("CREATE INDEX IF NOT EXISTS idx_tracks_date_added ON tracks(date_added)");
    executeSQL("CREATE INDEX IF NOT EXISTS idx_tracks_file_path ON tracks(file_path)");
    executeSQL("CREATE INDEX IF NOT EXISTS idx_cue_points_track ON cue_points(track_id)");
    executeSQL("CREATE INDEX IF NOT EXISTS idx_playlist_tracks_playlist ON playlist_tracks(playlist_id)");
    executeSQL("CREATE INDEX IF NOT EXISTS idx_play_history_track ON play_history(track_id)");
    executeSQL("CREATE INDEX IF NOT EXISTS idx_play_history_date ON play_history(played_at)");
    return true;
}

bool TrackDatabase::setupFTS5() {
    if (!executeSQL(
        "CREATE VIRTUAL TABLE IF NOT EXISTS tracks_fts USING fts5("
        "title, artist, album, genre, comment, label, grouping, mood, "
        "content='tracks', "
        "content_rowid='id', "
        "tokenize='unicode61 remove_diacritics 2'"
        ")")) {
        spdlog::warn("TrackDatabase: FTS5 creation failed");
        return false;
    }

    executeSQL("CREATE TRIGGER IF NOT EXISTS tracks_ai AFTER INSERT ON tracks BEGIN "
               "INSERT INTO tracks_fts(rowid, title, artist, album, genre, comment, label, grouping, mood) "
               "VALUES (new.id, new.title, new.artist, new.album, new.genre, new.comment, new.label, new.grouping, new.mood); "
               "END");

    executeSQL("CREATE TRIGGER IF NOT EXISTS tracks_ad AFTER DELETE ON tracks BEGIN "
               "INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album, genre, comment, label, grouping, mood) "
               "VALUES ('delete', old.id, old.title, old.artist, old.album, old.genre, old.comment, old.label, old.grouping, old.mood); "
               "END");

    executeSQL("CREATE TRIGGER IF NOT EXISTS tracks_au AFTER UPDATE ON tracks BEGIN "
               "INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album, genre, comment, label, grouping, mood) "
               "VALUES ('delete', old.id, old.title, old.artist, old.album, old.genre, old.comment, old.label, old.grouping, old.mood); "
               "INSERT INTO tracks_fts(rowid, title, artist, album, genre, comment, label, grouping, mood) "
               "VALUES (new.id, new.title, new.artist, new.album, new.genre, new.comment, new.label, new.grouping, new.mood); "
               "END");

    spdlog::info("TrackDatabase: FTS5 setup complete");
    return true;
}

bool TrackDatabase::rebuildFTSIndex() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!executeSQL("INSERT INTO tracks_fts(tracks_fts) VALUES('rebuild')")) {
        spdlog::error("TrackDatabase: FTS rebuild failed");
        return false;
    }
    spdlog::info("TrackDatabase: FTS index rebuilt");
    return true;
}

int64_t TrackDatabase::addTrack(const Models::Track& track) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;

    const char* sql =
        "INSERT INTO tracks (file_path, title, artist, album, genre, year, comment, "
        "duration, sample_rate, channels, bit_rate, bit_depth, "
        "bpm, key, energy, rating, play_count, last_played, "
        "color, label, grouping, date_added, last_modified, "
        "file_size, file_format, analyzed, analyzed_date, "
        "camelot_key, open_key, mood, danceability, lufs, energy_segments, "
        "bpm_confidence, key_confidence, true_peak, loudness_range, sections, source, "
        "intro_start, intro_end, outro_start, outro_end, role, venue) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("TrackDatabase: addTrack prepare failed: {}", sqlite3_errmsg(db_));
        return -1;
    }

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    int i = 1;
    bindString(stmt, i++, track.filePath);
    bindString(stmt, i++, track.title);
    bindString(stmt, i++, track.artist);
    bindString(stmt, i++, track.album);
    bindString(stmt, i++, track.genre);
    sqlite3_bind_int(stmt, i++, track.year);
    bindString(stmt, i++, track.comment);
    sqlite3_bind_double(stmt, i++, track.duration);
    sqlite3_bind_int(stmt, i++, track.sampleRate);
    sqlite3_bind_int(stmt, i++, track.channels);
    sqlite3_bind_int(stmt, i++, track.bitRate);
    sqlite3_bind_int(stmt, i++, track.bitDepth);
    sqlite3_bind_double(stmt, i++, track.bpm);
    bindString(stmt, i++, track.key);
    sqlite3_bind_double(stmt, i++, static_cast<double>(track.energy));
    sqlite3_bind_int(stmt, i++, track.rating);
    sqlite3_bind_int(stmt, i++, track.playCount);
    sqlite3_bind_int64(stmt, i++, track.lastPlayed);
    bindString(stmt, i++, track.color);
    bindString(stmt, i++, track.label);
    bindString(stmt, i++, track.grouping);
    sqlite3_bind_int64(stmt, i++, track.dateAdded > 0 ? track.dateAdded : now);
    sqlite3_bind_int64(stmt, i++, track.lastModified > 0 ? track.lastModified : now);
    sqlite3_bind_int64(stmt, i++, track.fileSize);
    bindString(stmt, i++, track.fileFormat);
    sqlite3_bind_int(stmt, i++, track.analyzed ? 1 : 0);
    sqlite3_bind_int64(stmt, i++, track.analyzedDate);
    bindString(stmt, i++, track.camelotKey);
    bindString(stmt, i++, track.openKey);
    bindString(stmt, i++, track.mood);
    sqlite3_bind_double(stmt, i++, static_cast<double>(track.danceability));
    sqlite3_bind_double(stmt, i++, static_cast<double>(track.lufs));
    bindString(stmt, i++, track.energySegments);
    sqlite3_bind_double(stmt, i++, static_cast<double>(track.bpmConfidence));
    sqlite3_bind_double(stmt, i++, static_cast<double>(track.keyConfidence));
    sqlite3_bind_double(stmt, i++, static_cast<double>(track.truePeak));
    sqlite3_bind_double(stmt, i++, static_cast<double>(track.loudnessRange));
    bindString(stmt, i++, track.sections);
    sqlite3_bind_int(stmt, i++, static_cast<int>(track.source));
    sqlite3_bind_double(stmt, i++, track.introStart);
    sqlite3_bind_double(stmt, i++, track.introEnd);
    sqlite3_bind_double(stmt, i++, track.outroStart);
    sqlite3_bind_double(stmt, i++, track.outroEnd);
    bindString(stmt, i++, track.role);
    bindString(stmt, i++, track.venue);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        spdlog::error("TrackDatabase: addTrack failed: {}", sqlite3_errmsg(db_));
        sqlite3_finalize(stmt);
        return -1;
    }

    int64_t id = sqlite3_last_insert_rowid(db_);
    sqlite3_finalize(stmt);
    spdlog::debug("TrackDatabase: Added track id={} '{}'", id, track.title);
    return id;
}

bool TrackDatabase::updateTrack(const Models::Track& track) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;

    const char* sql =
        "UPDATE tracks SET file_path=?, title=?, artist=?, album=?, genre=?, year=?, comment=?, "
        "duration=?, sample_rate=?, channels=?, bit_rate=?, bit_depth=?, "
        "bpm=?, key=?, energy=?, rating=?, play_count=?, last_played=?, "
        "color=?, label=?, grouping=?, last_modified=?, "
        "file_size=?, file_format=?, analyzed=?, analyzed_date=?, "
        "camelot_key=?, open_key=?, mood=?, danceability=?, lufs=?, energy_segments=?, "
        "bpm_confidence=?, key_confidence=?, true_peak=?, loudness_range=?, sections=?, source=?, "
        "intro_start=?, intro_end=?, outro_start=?, outro_end=?, role=?, venue=? "
        "WHERE id=?";

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("TrackDatabase: updateTrack prepare failed: {}", sqlite3_errmsg(db_));
        return false;
    }

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    int i = 1;
    bindString(stmt, i++, track.filePath);
    bindString(stmt, i++, track.title);
    bindString(stmt, i++, track.artist);
    bindString(stmt, i++, track.album);
    bindString(stmt, i++, track.genre);
    sqlite3_bind_int(stmt, i++, track.year);
    bindString(stmt, i++, track.comment);
    sqlite3_bind_double(stmt, i++, track.duration);
    sqlite3_bind_int(stmt, i++, track.sampleRate);
    sqlite3_bind_int(stmt, i++, track.channels);
    sqlite3_bind_int(stmt, i++, track.bitRate);
    sqlite3_bind_int(stmt, i++, track.bitDepth);
    sqlite3_bind_double(stmt, i++, track.bpm);
    bindString(stmt, i++, track.key);
    sqlite3_bind_double(stmt, i++, static_cast<double>(track.energy));
    sqlite3_bind_int(stmt, i++, track.rating);
    sqlite3_bind_int(stmt, i++, track.playCount);
    sqlite3_bind_int64(stmt, i++, track.lastPlayed);
    bindString(stmt, i++, track.color);
    bindString(stmt, i++, track.label);
    bindString(stmt, i++, track.grouping);
    sqlite3_bind_int64(stmt, i++, now);
    sqlite3_bind_int64(stmt, i++, track.fileSize);
    bindString(stmt, i++, track.fileFormat);
    sqlite3_bind_int(stmt, i++, track.analyzed ? 1 : 0);
    sqlite3_bind_int64(stmt, i++, track.analyzedDate);
    bindString(stmt, i++, track.camelotKey);
    bindString(stmt, i++, track.openKey);
    bindString(stmt, i++, track.mood);
    sqlite3_bind_double(stmt, i++, static_cast<double>(track.danceability));
    sqlite3_bind_double(stmt, i++, static_cast<double>(track.lufs));
    bindString(stmt, i++, track.energySegments);
    sqlite3_bind_double(stmt, i++, static_cast<double>(track.bpmConfidence));
    sqlite3_bind_double(stmt, i++, static_cast<double>(track.keyConfidence));
    sqlite3_bind_double(stmt, i++, static_cast<double>(track.truePeak));
    sqlite3_bind_double(stmt, i++, static_cast<double>(track.loudnessRange));
    bindString(stmt, i++, track.sections);
    sqlite3_bind_int(stmt, i++, static_cast<int>(track.source));
    sqlite3_bind_double(stmt, i++, track.introStart);
    sqlite3_bind_double(stmt, i++, track.introEnd);
    sqlite3_bind_double(stmt, i++, track.outroStart);
    sqlite3_bind_double(stmt, i++, track.outroEnd);
    bindString(stmt, i++, track.role);
    bindString(stmt, i++, track.venue);
    sqlite3_bind_int64(stmt, i++, track.id);

    rc = sqlite3_step(stmt);
    bool success = (rc == SQLITE_DONE) && (sqlite3_changes(db_) > 0);
    if (rc != SQLITE_DONE) {
        spdlog::error("TrackDatabase: updateTrack failed for id={}: {}", track.id, sqlite3_errmsg(db_));
    }

    sqlite3_finalize(stmt);
    return success;
}

bool TrackDatabase::deleteTrack(int64_t id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(db_, "DELETE FROM tracks WHERE id=?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("TrackDatabase: deleteTrack prepare failed: {}", sqlite3_errmsg(db_));
        return false;
    }

    sqlite3_bind_int64(stmt, 1, id);
    rc = sqlite3_step(stmt);
    bool success = (rc == SQLITE_DONE) && (sqlite3_changes(db_) > 0);
    sqlite3_finalize(stmt);

    if (success) {
        spdlog::debug("TrackDatabase: Deleted track id={}", id);
    }
    return success;
}

std::optional<Models::Track> TrackDatabase::getTrack(int64_t id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(db_, "SELECT * FROM tracks WHERE id=?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return std::nullopt;

    sqlite3_bind_int64(stmt, 1, id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        auto track = trackFromStatement(stmt);
        sqlite3_finalize(stmt);
        return track;
    }

    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::optional<Models::Track> TrackDatabase::getTrackByPath(const std::string& filePath) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(db_, "SELECT * FROM tracks WHERE file_path=?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return std::nullopt;

    bindString(stmt, 1, filePath);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        auto track = trackFromStatement(stmt);
        sqlite3_finalize(stmt);
        return track;
    }

    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::vector<Models::Track> TrackDatabase::getAllTracks() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<Models::Track> tracks;
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(db_, "SELECT * FROM tracks ORDER BY id DESC", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("TrackDatabase: getAllTracks failed: {}", sqlite3_errmsg(db_));
        return tracks;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        tracks.push_back(trackFromStatement(stmt));
    }

    sqlite3_finalize(stmt);
    return tracks;
}

std::vector<Models::Track> TrackDatabase::getTracksByQuery(const std::string& sql, const std::vector<std::string>& params) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<Models::Track> tracks;
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("TrackDatabase: getTracksByQuery failed: {}", sqlite3_errmsg(db_));
        return tracks;
    }

    for (size_t i = 0; i < params.size(); ++i) {
        bindString(stmt, static_cast<int>(i + 1), params[i]);
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        tracks.push_back(trackFromStatement(stmt));
    }

    sqlite3_finalize(stmt);
    return tracks;
}

int64_t TrackDatabase::getTrackCount() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM tracks", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    int64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int64(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

int64_t TrackDatabase::addCuePoint(const Models::CuePoint& cue) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;

    const char* sql =
        "INSERT INTO cue_points (track_id, type, position, length, name, color, number) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)";

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("TrackDatabase: addCuePoint prepare failed: {}", sqlite3_errmsg(db_));
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, cue.trackId);
    sqlite3_bind_int(stmt, 2, static_cast<int>(cue.type));
    sqlite3_bind_double(stmt, 3, cue.position);
    sqlite3_bind_double(stmt, 4, cue.length);
    bindString(stmt, 5, cue.name);
    bindString(stmt, 6, cue.color);
    sqlite3_bind_int(stmt, 7, cue.number);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        spdlog::error("TrackDatabase: addCuePoint failed: {}", sqlite3_errmsg(db_));
        sqlite3_finalize(stmt);
        return -1;
    }

    int64_t id = sqlite3_last_insert_rowid(db_);
    sqlite3_finalize(stmt);
    return id;
}

bool TrackDatabase::updateCuePoint(const Models::CuePoint& cue) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;

    const char* sql =
        "UPDATE cue_points SET type=?, position=?, length=?, name=?, color=?, number=? WHERE id=?";

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("TrackDatabase: updateCuePoint prepare failed: {}", sqlite3_errmsg(db_));
        return false;
    }

    sqlite3_bind_int(stmt, 1, static_cast<int>(cue.type));
    sqlite3_bind_double(stmt, 2, cue.position);
    sqlite3_bind_double(stmt, 3, cue.length);
    bindString(stmt, 4, cue.name);
    bindString(stmt, 5, cue.color);
    sqlite3_bind_int(stmt, 6, cue.number);
    sqlite3_bind_int64(stmt, 7, cue.id);

    rc = sqlite3_step(stmt);
    bool success = (rc == SQLITE_DONE) && (sqlite3_changes(db_) > 0);
    sqlite3_finalize(stmt);
    return success;
}

bool TrackDatabase::deleteCuePoint(int64_t id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(db_, "DELETE FROM cue_points WHERE id=?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int64(stmt, 1, id);
    rc = sqlite3_step(stmt);
    bool success = (rc == SQLITE_DONE) && (sqlite3_changes(db_) > 0);
    sqlite3_finalize(stmt);
    return success;
}

std::vector<Models::CuePoint> TrackDatabase::getCuePoints(int64_t trackId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<Models::CuePoint> cuePoints;
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(db_, "SELECT * FROM cue_points WHERE track_id=? ORDER BY position", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("TrackDatabase: getCuePoints failed: {}", sqlite3_errmsg(db_));
        return cuePoints;
    }

    sqlite3_bind_int64(stmt, 1, trackId);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        cuePoints.push_back(cuePointFromStatement(stmt));
    }

    sqlite3_finalize(stmt);
    return cuePoints;
}

std::string TrackDatabase::getSyncWatermark(const std::string& software) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    std::string wm;
    if (sqlite3_prepare_v2(db_, "SELECT watermark FROM sync_state WHERE software=?", -1, &stmt, nullptr) != SQLITE_OK)
        return wm;
    sqlite3_bind_text(stmt, 1, software.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        if (const auto* txt = sqlite3_column_text(stmt, 0))
            wm = reinterpret_cast<const char*>(txt);
    }
    sqlite3_finalize(stmt);
    return wm;
}

void TrackDatabase::setSyncWatermark(const std::string& software, const std::string& watermark) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "INSERT INTO sync_state (software, watermark, last_sync, sync_count) "
        "VALUES (?, ?, strftime('%s','now'), 1) "
        "ON CONFLICT(software) DO UPDATE SET watermark=excluded.watermark, "
        "last_sync=excluded.last_sync, sync_count=sync_count+1",
        -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("TrackDatabase: setSyncWatermark failed: {}", sqlite3_errmsg(db_));
        return;
    }
    sqlite3_bind_text(stmt, 1, software.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, watermark.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

int TrackDatabase::getSyncCount(const std::string& software) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(db_, "SELECT sync_count FROM sync_state WHERE software=?", -1, &stmt, nullptr) != SQLITE_OK)
        return count;
    sqlite3_bind_text(stmt, 1, software.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

bool TrackDatabase::upsertTrackEmbedding(int64_t trackId, const std::vector<float>& vec,
                                          const std::string& modelVersion, int64_t fileMtime) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || trackId <= 0 || vec.empty()) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "INSERT INTO track_embeddings (track_id, dim, vec, model_version, file_mtime, computed_at) "
        "VALUES (?, ?, ?, ?, ?, strftime('%s','now')) "
        "ON CONFLICT(track_id) DO UPDATE SET dim=excluded.dim, vec=excluded.vec, "
        "model_version=excluded.model_version, file_mtime=excluded.file_mtime, "
        "computed_at=excluded.computed_at", -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("TrackDatabase: upsertTrackEmbedding prepare failed: {}", sqlite3_errmsg(db_));
        return false;
    }
    sqlite3_bind_int64(stmt, 1, trackId);
    sqlite3_bind_int(stmt, 2, (int) vec.size());
    sqlite3_bind_blob(stmt, 3, vec.data(), (int) (vec.size() * sizeof(float)), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, modelVersion.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, fileMtime);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok)
        spdlog::error("TrackDatabase: upsertTrackEmbedding failed: {}", sqlite3_errmsg(db_));
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<TrackDatabase::TrackEmbedding> TrackDatabase::loadAllTrackEmbeddings(const std::string& modelVersion) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<TrackEmbedding> out;
    if (!db_) return out;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT track_id, dim, vec, file_mtime FROM track_embeddings WHERE model_version=?",
        -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("TrackDatabase: loadAllTrackEmbeddings prepare failed: {}", sqlite3_errmsg(db_));
        return out;
    }
    sqlite3_bind_text(stmt, 1, modelVersion.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const int dim = sqlite3_column_int(stmt, 1);
        const void* blob = sqlite3_column_blob(stmt, 2);
        const int bytes = sqlite3_column_bytes(stmt, 2);
        if (!blob || dim <= 0 || bytes != dim * (int) sizeof(float)) continue;
        TrackEmbedding e;
        e.trackId = sqlite3_column_int64(stmt, 0);
        e.vec.resize((size_t) dim);
        std::memcpy(e.vec.data(), blob, (size_t) bytes);
        e.fileMtime = sqlite3_column_int64(stmt, 3);
        out.push_back(std::move(e));
    }
    sqlite3_finalize(stmt);
    return out;
}

bool TrackDatabase::updateTrackGenreIfEmpty(int64_t trackId, const std::string& genre) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || trackId <= 0 || genre.empty()) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "UPDATE tracks SET genre=? WHERE id=? AND (genre IS NULL OR TRIM(genre)='')",
        -1, &stmt, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_text(stmt, 1, genre.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, trackId);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok && sqlite3_changes(db_) > 0;
}

bool TrackDatabase::updateTrackMoodIfEmpty(int64_t trackId, const std::string& mood) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || trackId <= 0 || mood.empty()) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "UPDATE tracks SET mood=? WHERE id=? AND (mood IS NULL OR TRIM(mood)='')",
        -1, &stmt, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_text(stmt, 1, mood.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, trackId);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok && sqlite3_changes(db_) > 0;
}

std::map<int64_t, int> TrackDatabase::getCueCounts() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::map<int64_t, int> counts;
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(db_, "SELECT track_id, COUNT(*) FROM cue_points GROUP BY track_id", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("TrackDatabase: getCueCounts failed: {}", sqlite3_errmsg(db_));
        return counts;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        counts[sqlite3_column_int64(stmt, 0)] = sqlite3_column_int(stmt, 1);
    }

    sqlite3_finalize(stmt);
    return counts;
}

std::vector<Models::Track> TrackDatabase::searchFTS(const std::string& query_str, int limit) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<Models::Track> tracks;
    sqlite3_stmt* stmt = nullptr;

    const char* sql =
        "SELECT t.* FROM tracks t "
        "JOIN tracks_fts fts ON t.id = fts.rowid "
        "WHERE tracks_fts MATCH ? "
        "ORDER BY rank "
        "LIMIT ?";

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("TrackDatabase: FTS search failed: {}", sqlite3_errmsg(db_));
        return tracks;
    }

    bindString(stmt, 1, query_str);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        tracks.push_back(trackFromStatement(stmt));
    }

    sqlite3_finalize(stmt);
    return tracks;
}

bool TrackDatabase::beginTransaction() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (txDepth_ > 0) {
        ++txDepth_;
        return true;
    }
    if (!executeSQL("BEGIN TRANSACTION")) return false;
    txDepth_ = 1;
    return true;
}

bool TrackDatabase::commitTransaction() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (txDepth_ > 1) {
        --txDepth_;
        return true;
    }
    txDepth_ = 0;
    return executeSQL("COMMIT");
}

bool TrackDatabase::rollbackTransaction() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (txDepth_ > 1) {
        --txDepth_;
        return true;
    }
    txDepth_ = 0;
    return executeSQL("ROLLBACK");
}

bool TrackDatabase::addTracks(const std::vector<Models::Track>& tracks) {
    if (!beginTransaction()) return false;

    for (const auto& track : tracks) {
        if (addTrack(track) < 0) {
            rollbackTransaction();
            return false;
        }
    }

    return commitTransaction();
}

bool TrackDatabase::deleteTracks(const std::vector<int64_t>& ids) {
    if (!beginTransaction()) return false;

    for (auto id : ids) {
        if (!deleteTrack(id)) {
            rollbackTransaction();
            return false;
        }
    }

    return commitTransaction();
}

static std::string safeColumnString(sqlite3_stmt* stmt, int col) {
    const unsigned char* text = sqlite3_column_text(stmt, col);
    return text ? reinterpret_cast<const char*>(text) : "";
}

Models::Track TrackDatabase::trackFromStatement(sqlite3_stmt* stmt) {
    Models::Track track;

    // Mapping par NOM de colonne : supporte le PascalCase V9 et le snake_case V11.
    int cols = sqlite3_column_count(stmt);
    for (int i = 0; i < cols; ++i) {
        const char* name = sqlite3_column_name(stmt, i);
        if (!name) continue;
        std::string col(name);
        std::string lc;
        for (char c : col) lc += static_cast<char>(std::tolower(c));

        if (lc == "id") track.id = sqlite3_column_int64(stmt, i);
        else if (lc == "filepath" || lc == "file_path") {
            auto val = safeColumnString(stmt, i);
            if (!val.empty()) track.filePath = val;
        }
        else if (lc == "title") track.title = safeColumnString(stmt, i);
        else if (lc == "artist") track.artist = safeColumnString(stmt, i);
        else if (lc == "album") track.album = safeColumnString(stmt, i);
        else if (lc == "genre") track.genre = safeColumnString(stmt, i);
        else if (lc == "year") track.year = sqlite3_column_int(stmt, i);
        else if (lc == "comment" || lc == "comments") track.comment = safeColumnString(stmt, i);
        else if (lc == "duration") {
            // V9 : duree en TEXT "HH:MM:SS"/"MM:SS" ou numerique ; V11 : REAL.
            if (sqlite3_column_type(stmt, i) == SQLITE_TEXT) {
                auto str = safeColumnString(stmt, i);
                int colonCount = 0;
                for (char c : str) if (c == ':') ++colonCount;
                if (colonCount == 2) {
                    int hh = 0, mm = 0; double ss = 0.0;
                    if (std::sscanf(str.c_str(), "%d:%d:%lf", &hh, &mm, &ss) == 3)
                        track.duration = hh * 3600.0 + mm * 60.0 + ss;
                    else
                        track.duration = std::atof(str.c_str());
                } else if (colonCount == 1) {
                    int mm = 0; double ss = 0.0;
                    if (std::sscanf(str.c_str(), "%d:%lf", &mm, &ss) == 2)
                        track.duration = mm * 60.0 + ss;
                    else
                        track.duration = std::atof(str.c_str());
                } else {
                    track.duration = std::atof(str.c_str());
                }
            } else {
                track.duration = sqlite3_column_double(stmt, i);
            }
        }
        else if (lc == "durationms") track.duration = sqlite3_column_int(stmt, i) / 1000.0;
        else if (lc == "samplerate" || lc == "sample_rate") track.sampleRate = sqlite3_column_int(stmt, i);
        else if (lc == "channels") track.channels = sqlite3_column_int(stmt, i);
        else if (lc == "bitrate" || lc == "bit_rate") track.bitRate = sqlite3_column_int(stmt, i);
        else if (lc == "bitdepth" || lc == "bit_depth") track.bitDepth = sqlite3_column_int(stmt, i);
        else if (lc == "bpm" || lc == "tempo") track.bpm = sqlite3_column_double(stmt, i);
        else if (lc == "key") track.key = safeColumnString(stmt, i);
        else if (lc == "energy") track.energy = static_cast<float>(sqlite3_column_double(stmt, i));
        else if (lc == "rating") track.rating = sqlite3_column_int(stmt, i);
        else if (lc == "playcount" || lc == "play_count") track.playCount = sqlite3_column_int(stmt, i);
        else if (lc == "lastplayed" || lc == "last_played") {
            // V9 : datetime TEXT ; V11 : timestamp INTEGER.
            if (sqlite3_column_type(stmt, i) == SQLITE_TEXT) track.lastPlayed = 0;
            else track.lastPlayed = sqlite3_column_int64(stmt, i);
        }
        else if (lc == "color") track.color = safeColumnString(stmt, i);
        else if (lc == "label") track.label = safeColumnString(stmt, i);
        else if (lc == "grouping") track.grouping = safeColumnString(stmt, i);
        else if (lc == "dateadded" || lc == "date_added" || lc == "createdat") {
            if (sqlite3_column_type(stmt, i) == SQLITE_TEXT) track.dateAdded = 0;
            else track.dateAdded = sqlite3_column_int64(stmt, i);
        }
        else if (lc == "lastmodified" || lc == "last_modified") {
            if (sqlite3_column_type(stmt, i) == SQLITE_TEXT) track.lastModified = 0;
            else track.lastModified = sqlite3_column_int64(stmt, i);
        }
        else if (lc == "filesize" || lc == "file_size") track.fileSize = sqlite3_column_int64(stmt, i);
        else if (lc == "fileformat" || lc == "file_format") track.fileFormat = safeColumnString(stmt, i);
        else if (lc == "isanalyzed" || lc == "analyzed") track.analyzed = sqlite3_column_int(stmt, i) != 0;
        else if (lc == "analyzed_date") track.analyzedDate = sqlite3_column_int64(stmt, i);
        else if (lc == "camelotkey" || lc == "camelot_key") track.camelotKey = safeColumnString(stmt, i);
        else if (lc == "openkey" || lc == "open_key") track.openKey = safeColumnString(stmt, i);
        else if (lc == "mood") track.mood = safeColumnString(stmt, i);
        else if (lc == "danceability") track.danceability = static_cast<float>(sqlite3_column_double(stmt, i));
        else if (lc == "source" || lc == "djsoftware") {
            if (sqlite3_column_type(stmt, i) == SQLITE_TEXT) track.source = Models::TrackSource::Local;
            else track.source = static_cast<Models::TrackSource>(sqlite3_column_int(stmt, i));
        }
        else if (lc == "intro_start"  || lc == "introstart")  track.introStart = sqlite3_column_double(stmt, i);
        else if (lc == "intro_end"    || lc == "introend")    track.introEnd   = sqlite3_column_double(stmt, i);
        else if (lc == "outro_start"  || lc == "outrostart")  track.outroStart = sqlite3_column_double(stmt, i);
        else if (lc == "outro_end"    || lc == "outroend")    track.outroEnd   = sqlite3_column_double(stmt, i);
        else if (lc == "role")  track.role  = safeColumnString(stmt, i);
        else if (lc == "venue") track.venue = safeColumnString(stmt, i);
        else if (lc == "valence") track.danceability = static_cast<float>(sqlite3_column_double(stmt, i));
        else if (lc == "lufs") track.lufs = static_cast<float>(sqlite3_column_double(stmt, i));
        else if (lc == "energy_segments" || lc == "energysegments") track.energySegments = safeColumnString(stmt, i);
        else if (lc == "bpm_confidence" || lc == "bpmconfidence") track.bpmConfidence = static_cast<float>(sqlite3_column_double(stmt, i));
        else if (lc == "key_confidence" || lc == "keyconfidence") track.keyConfidence = static_cast<float>(sqlite3_column_double(stmt, i));
        else if (lc == "true_peak" || lc == "truepeak") track.truePeak = static_cast<float>(sqlite3_column_double(stmt, i));
        else if (lc == "loudness_range" || lc == "loudnessrange") track.loudnessRange = static_cast<float>(sqlite3_column_double(stmt, i));
        else if (lc == "sections") track.sections = safeColumnString(stmt, i);
        else if (lc == "loudness") { /* skip for now */ }
        else if (lc == "popularity") { /* skip */ }
    }

    return track;
}

Models::CuePoint TrackDatabase::cuePointFromStatement(sqlite3_stmt* stmt) {
    Models::CuePoint cue;
    int col = 0;
    cue.id = sqlite3_column_int64(stmt, col++);
    cue.trackId = sqlite3_column_int64(stmt, col++);
    cue.type = static_cast<Models::CuePointType>(sqlite3_column_int(stmt, col++));
    cue.position = sqlite3_column_double(stmt, col++);
    cue.length = sqlite3_column_double(stmt, col++);
    cue.name = safeColumnString(stmt, col++);
    cue.color = safeColumnString(stmt, col++);
    cue.number = sqlite3_column_int(stmt, col++);
    return cue;
}

int TrackDatabase::getCurrentSchemaVersion() {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, "SELECT version FROM schema_version LIMIT 1", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    int version = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        version = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return version;
}

bool TrackDatabase::setSchemaVersion(int version) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, "UPDATE schema_version SET version=?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, version);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool TrackDatabase::updateTrackAnalysis(int64_t trackId, double bpm, const std::string& key, float energy) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return false;

    const char* sql = "UPDATE tracks SET bpm=?, key=?, energy=?, camelot_key=?, analyzed=1 WHERE id=? AND (bpm=0 OR bpm IS NULL)";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("TrackDatabase::updateTrackAnalysis prepare failed: {}", sqlite3_errmsg(db_));
        return false;
    }

    sqlite3_bind_double(stmt, 1, bpm);
    bindString(stmt, 2, key);
    sqlite3_bind_double(stmt, 3, static_cast<double>(energy));
    bindString(stmt, 4, key);
    sqlite3_bind_int64(stmt, 5, trackId);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        int changes = sqlite3_changes(db_);
        if (changes > 0) {
            spdlog::info("TrackDatabase: Updated analysis for track {} (BPM={:.1f}, Key={}, Energy={:.1f})", trackId, bpm, key, energy);
        }
        return true;
    }

    spdlog::error("TrackDatabase::updateTrackAnalysis step failed: {}", sqlite3_errmsg(db_));
    return false;
}

bool TrackDatabase::updateTrackAnalysisFromSync(int64_t trackId, double bpm, const std::string& key,
                                                float energy, const std::string& source) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return false;

    const char* sql = "UPDATE tracks SET bpm=?, key=?, energy=?, camelot_key=?, analyzed=1, analysis_source=? WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("TrackDatabase::updateTrackAnalysisFromSync prepare failed: {}", sqlite3_errmsg(db_));
        return false;
    }

    sqlite3_bind_double(stmt, 1, bpm);
    bindString(stmt, 2, key);
    sqlite3_bind_double(stmt, 3, static_cast<double>(energy));
    bindString(stmt, 4, key);
    bindString(stmt, 5, source);
    sqlite3_bind_int64(stmt, 6, trackId);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        spdlog::info("TrackDatabase: Sync analysis for track {} from '{}' (BPM={:.1f}, Key={})",
                     trackId, source, bpm, key);
        return true;
    }

    spdlog::error("TrackDatabase::updateTrackAnalysisFromSync step failed: {}", sqlite3_errmsg(db_));
    return false;
}

std::string TrackDatabase::getAnalysisSource(int64_t trackId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return {};

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT analysis_source FROM tracks WHERE id=?", -1, &stmt, nullptr) != SQLITE_OK)
        return {};

    sqlite3_bind_int64(stmt, 1, trackId);
    std::string out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        if (const auto* txt = sqlite3_column_text(stmt, 0))
            out = reinterpret_cast<const char*>(txt);
    }
    sqlite3_finalize(stmt);
    return out;
}

bool TrackDatabase::setAnalysisSource(int64_t trackId, const std::string& source) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "UPDATE tracks SET analysis_source=? WHERE id=?", -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    bindString(stmt, 1, source);
    sqlite3_bind_int64(stmt, 2, trackId);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool TrackDatabase::updateTrackAnalysisByPath(const std::string& filePath, double bpm, const std::string& key, float energy) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return false;

    const char* sql = "UPDATE tracks SET bpm=?, key=?, energy=?, camelot_key=?, analyzed=1 WHERE file_path=? AND (bpm=0 OR bpm IS NULL)";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_double(stmt, 1, bpm);
    bindString(stmt, 2, key);
    sqlite3_bind_double(stmt, 3, static_cast<double>(energy));
    bindString(stmt, 4, key);
    bindString(stmt, 5, filePath);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool TrackDatabase::importCuePointsForTrack(int64_t trackId, const std::vector<Models::CuePoint>& cuePoints) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return false;

    sqlite3_stmt* checkStmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM cue_points WHERE track_id=?", -1, &checkStmt, nullptr);
    sqlite3_bind_int64(checkStmt, 1, trackId);
    int existingCount = 0;
    if (sqlite3_step(checkStmt) == SQLITE_ROW) {
        existingCount = sqlite3_column_int(checkStmt, 0);
    }
    sqlite3_finalize(checkStmt);

    if (existingCount > 0) {
        return true;
    }

    executeSQL("BEGIN TRANSACTION");
    for (const auto& cue : cuePoints) {
        Models::CuePoint c = cue;
        c.trackId = trackId;
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "INSERT INTO cue_points (track_id, type, position, length, name, color, number) VALUES (?,?,?,?,?,?,?)";
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) continue;

        sqlite3_bind_int64(stmt, 1, c.trackId);
        sqlite3_bind_int(stmt, 2, static_cast<int>(c.type));
        sqlite3_bind_double(stmt, 3, c.position);
        sqlite3_bind_double(stmt, 4, c.length);
        bindString(stmt, 5, c.name);
        bindString(stmt, 6, c.color);
        sqlite3_bind_int(stmt, 7, c.number);

        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    executeSQL("COMMIT");

    spdlog::info("TrackDatabase: Imported {} cue points for track {}", cuePoints.size(), trackId);
    return true;
}

} // namespace BeatMate::Services::Library
