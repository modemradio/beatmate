#include "PioneerDatabase.h"

#include <spdlog/spdlog.h>

namespace BeatMate::Services::PioneerDJ {

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

PioneerDatabase::PioneerDatabase() {
}

PioneerDatabase::~PioneerDatabase() {
    close();
}

bool PioneerDatabase::openDatabase(const std::string& path, const std::string& key) {
    int rc = sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("PioneerDatabase: Failed to open: {}", sqlite3_errmsg(db_));
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    // If encrypted (Rekordbox 6+), would need SQLCipher
    if (!key.empty()) {
        std::string pragmaKey = "PRAGMA key = '" + escapeSqlLiteral(key) + "'";
        sqlite3_exec(db_, pragmaKey.c_str(), nullptr, nullptr, nullptr);
    }

    sqlite3_stmt* testStmt = nullptr;
    rc = sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM sqlite_master", -1, &testStmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("PioneerDatabase: Database not readable (encrypted?): {}", sqlite3_errmsg(db_));
        sqlite3_finalize(testStmt);
        close();
        return false;
    }
    sqlite3_finalize(testStmt);

    isOpen_ = true;
    spdlog::info("PioneerDatabase: Opened database at {}", path);
    return true;
}

void PioneerDatabase::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
    isOpen_ = false;
}

bool PioneerDatabase::isOpen() const {
    return isOpen_;
}

std::vector<Models::PioneerTrack> PioneerDatabase::readAllTracks() {
    std::vector<Models::PioneerTrack> tracks;
    if (!isOpen_) return tracks;

    sqlite3_stmt* stmt = nullptr;
    bool isV6 = true;

    const char* sqlV6 =
        "SELECT ID, FolderPath, Title, ArtistName, AlbumName, GenreName, "
        "BPM, ColorID, RatingValue, Comment, Label, "
        "Duration, Year, DateCreated, Tonality, "
        "BitRate, SampleRate, AnalysisStatus "
        "FROM djmdContent ORDER BY Title";

    int rc = sqlite3_prepare_v2(db_, sqlV6, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        isV6 = false;
        const char* sqlOld =
            "SELECT rb_local_content_id, file_path, title, artist, album, genre, "
            "bpm, color, rating, comment, label, "
            "duration, year, date_added, key "
            "FROM content ORDER BY title";

        rc = sqlite3_prepare_v2(db_, sqlOld, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            spdlog::error("PioneerDatabase: Failed to read tracks: {}", sqlite3_errmsg(db_));
            return tracks;
        }
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        tracks.push_back(trackFromStatement(stmt, isV6));
    }

    sqlite3_finalize(stmt);
    spdlog::info("PioneerDatabase: Read {} tracks", tracks.size());
    return tracks;
}

std::vector<Models::Playlist> PioneerDatabase::readPlaylists() {
    std::vector<Models::Playlist> playlists;
    if (!isOpen_) return playlists;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, "SELECT ID, Name, Seq FROM djmdPlaylist ORDER BY Seq", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("PioneerDatabase: Failed to read playlists: {}", sqlite3_errmsg(db_));
        return playlists;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Models::Playlist pl;
        pl.id = sqlite3_column_int64(stmt, 0);
        pl.name = getColumnText(stmt, 1);
        playlists.push_back(pl);
    }

    sqlite3_finalize(stmt);
    return playlists;
}

std::vector<Models::PioneerDJCue> PioneerDatabase::readCuePoints(const std::string& contentId) {
    std::vector<Models::PioneerDJCue> cues;
    if (!isOpen_) return cues;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT HotCueNum, InMsec, OutMsec, ColorID FROM djmdCue WHERE ContentID = ? ORDER BY HotCueNum",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("PioneerDatabase: Failed to read cue points: {}", sqlite3_errmsg(db_));
        return cues;
    }

    sqlite3_bind_text(stmt, 1, contentId.c_str(), static_cast<int>(contentId.size()), SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Models::PioneerDJCue cue;
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

Models::PioneerTrack PioneerDatabase::trackFromStatement(sqlite3_stmt* stmt, bool isV6Schema) {
    Models::PioneerTrack track;

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

} // namespace BeatMate::Services::PioneerDJ
