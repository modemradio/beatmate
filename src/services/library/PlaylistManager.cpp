#include "PlaylistManager.h"
#include "TrackDatabase.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace BeatMate::Services::Library {

PlaylistManager::PlaylistManager(std::shared_ptr<TrackDatabase> database)
    : database_(std::move(database)) {
}

int64_t PlaylistManager::createPlaylist(const std::string& name, const std::string& description) {
    if (!database_ || !database_->isOpen()) {
        spdlog::error("PlaylistManager: No database connection for createPlaylist");
        return -1;
    }

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (!database_->executeWrite(
            "INSERT INTO playlists (name, description, created_at, modified_at) "
            "VALUES (?, ?, ?, ?)",
            { name, description, std::to_string(now), std::to_string(now) })) {
        spdlog::error("PlaylistManager: createPlaylist INSERT failed");
        return -1;
    }

    int64_t playlistId = database_->getLastInsertRowId();
    if (playlistId <= 0) {
        spdlog::error("PlaylistManager: createPlaylist last_insert_rowid returned {}", playlistId);
        return -1;
    }
    spdlog::info("PlaylistManager: Created playlist '{}' with id={}", name, playlistId);
    return playlistId;
}

bool PlaylistManager::deletePlaylist(int64_t playlistId) {
    if (!database_ || !database_->isOpen()) {
        spdlog::error("PlaylistManager: No database connection for deletePlaylist");
        return false;
    }

    // suppression explicite des tracks même si le cascade devrait suffire
    bool ok = database_->executeWrite(
        "DELETE FROM playlist_tracks WHERE playlist_id = ?",
        { std::to_string(playlistId) });

    ok = database_->executeWrite(
        "DELETE FROM playlists WHERE id = ?",
        { std::to_string(playlistId) }) && ok;

    spdlog::info("PlaylistManager: Deleted playlist {} (ok={})", playlistId, ok);
    return ok;
}

bool PlaylistManager::renamePlaylist(int64_t playlistId, const std::string& name) {
    if (!database_ || !database_->isOpen()) {
        spdlog::error("PlaylistManager: No database connection for renamePlaylist");
        return false;
    }

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    const bool ok = database_->executeWrite(
        "UPDATE playlists SET name = ?, modified_at = ? WHERE id = ?",
        { name, std::to_string(now), std::to_string(playlistId) });

    spdlog::info("PlaylistManager: Renamed playlist {} to '{}' (ok={})", playlistId, name, ok);
    return ok;
}

std::optional<Models::Playlist> PlaylistManager::getPlaylist(int64_t playlistId) {
    if (!database_ || !database_->isOpen()) {
        return std::nullopt;
    }

    // lecture par index de colonne (trackFromStatement mappe par nom)
    bool found = false;
    Models::Playlist playlist;
    database_->executeRead(
        "SELECT id, name, description, is_smart FROM playlists WHERE id = ?",
        {std::to_string(playlistId)},
        [&](sqlite3_stmt* stmt) {
            found = true;
            playlist.id = sqlite3_column_int64(stmt, 0);
            if (auto t = sqlite3_column_text(stmt, 1)) playlist.name = reinterpret_cast<const char*>(t);
            if (auto t = sqlite3_column_text(stmt, 2)) playlist.description = reinterpret_cast<const char*>(t);
            playlist.isSmartPlaylist = sqlite3_column_int(stmt, 3) != 0;
        });

    if (!found) return std::nullopt;

    database_->executeRead(
        "SELECT track_id FROM playlist_tracks WHERE playlist_id = ? ORDER BY position",
        {std::to_string(playlistId)},
        [&](sqlite3_stmt* stmt) {
            playlist.trackIds.push_back(sqlite3_column_int64(stmt, 0));
        });

    return playlist;
}

std::vector<Models::Playlist> PlaylistManager::getAllPlaylists() {
    if (!database_ || !database_->isOpen()) {
        return {};
    }

    // Lecture par index de colonne (cf. getPlaylist) pour ne pas perdre le nom.
    std::vector<Models::Playlist> playlists;
    database_->executeRead(
        "SELECT id, name, description, is_smart FROM playlists ORDER BY name",
        {},
        [&](sqlite3_stmt* stmt) {
            Models::Playlist p;
            p.id = sqlite3_column_int64(stmt, 0);
            if (auto t = sqlite3_column_text(stmt, 1)) p.name = reinterpret_cast<const char*>(t);
            if (auto t = sqlite3_column_text(stmt, 2)) p.description = reinterpret_cast<const char*>(t);
            p.isSmartPlaylist = sqlite3_column_int(stmt, 3) != 0;
            playlists.push_back(std::move(p));
        });

    return playlists;
}

bool PlaylistManager::addTrack(int64_t playlistId, int64_t trackId) {
    if (!database_ || !database_->isOpen()) {
        spdlog::error("PlaylistManager: No database connection for addTrack");
        return false;
    }

    int position = getTrackCount(playlistId);
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    bool ok = database_->executeWrite(
        "INSERT OR IGNORE INTO playlist_tracks (playlist_id, track_id, position, added_at) "
        "VALUES (?, ?, ?, ?)",
        { std::to_string(playlistId), std::to_string(trackId),
          std::to_string(position),  std::to_string(now) });

    ok = database_->executeWrite(
        "UPDATE playlists SET modified_at = ? WHERE id = ?",
        { std::to_string(now), std::to_string(playlistId) }) && ok;

    spdlog::debug("PlaylistManager: Added track {} to playlist {} at position {} (ok={})",
                  trackId, playlistId, position, ok);
    return ok;
}

bool PlaylistManager::removeTrack(int64_t playlistId, int64_t trackId) {
    if (!database_ || !database_->isOpen()) {
        spdlog::error("PlaylistManager: No database connection for removeTrack");
        return false;
    }

    bool ok = database_->executeWrite(
        "DELETE FROM playlist_tracks WHERE playlist_id = ? AND track_id = ?",
        {std::to_string(playlistId), std::to_string(trackId)}
    );

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    ok = database_->executeWrite(
        "UPDATE playlists SET modified_at = ? WHERE id = ?",
        {std::to_string(now), std::to_string(playlistId)}
    ) && ok;

    spdlog::debug("PlaylistManager: Removed track {} from playlist {} (ok={})", trackId, playlistId, ok);
    return ok;
}

bool PlaylistManager::reorderTracks(int64_t playlistId, const std::vector<int64_t>& trackIds) {
    if (!database_ || !database_->isOpen()) {
        spdlog::error("PlaylistManager: No database connection for reorderTracks");
        return false;
    }

    bool ok = true;
    for (size_t i = 0; i < trackIds.size(); ++i) {
        ok = database_->executeWrite(
            "UPDATE playlist_tracks SET position = ? WHERE playlist_id = ? AND track_id = ?",
            {std::to_string(static_cast<int>(i)), std::to_string(playlistId), std::to_string(trackIds[i])}
        ) && ok;
    }

    spdlog::debug("PlaylistManager: Reordered {} tracks in playlist {} (ok={})", trackIds.size(), playlistId, ok);
    return ok;
}

std::vector<Models::Track> PlaylistManager::getPlaylistTracks(int64_t playlistId) {
    if (!database_ || !database_->isOpen()) {
        spdlog::debug("PlaylistManager: No database, skipping getPlaylistTracks");
        return {};
    }
    return database_->getTracksByQuery(
        "SELECT t.* FROM tracks t "
        "JOIN playlist_tracks pt ON t.id = pt.track_id "
        "WHERE pt.playlist_id = ? "
        "ORDER BY pt.position",
        {std::to_string(playlistId)}
    );
}

int PlaylistManager::getTrackCount(int64_t playlistId) {
    if (!database_ || !database_->isOpen()) {
        return 0;
    }

    int count = 0;
    database_->executeRead(
        "SELECT COUNT(*) FROM playlist_tracks WHERE playlist_id = ?",
        {std::to_string(playlistId)},
        [&](sqlite3_stmt* stmt) {
            count = sqlite3_column_int(stmt, 0);
        });
    return count;
}

bool PlaylistManager::exportPlaylist(int64_t playlistId, const std::string& outputPath,
                                      PlaylistExportFormat format) {
    auto playlistOpt = getPlaylist(playlistId);
    if (!playlistOpt) {
        spdlog::error("PlaylistManager: Playlist {} not found for export", playlistId);
        return false;
    }

    auto tracks = getPlaylistTracks(playlistId);

    switch (format) {
        case PlaylistExportFormat::M3U:
            return exportM3U(*playlistOpt, tracks, outputPath, false);
        case PlaylistExportFormat::M3U8:
            return exportM3U(*playlistOpt, tracks, outputPath, true);
        case PlaylistExportFormat::PLS:
            return exportPLS(*playlistOpt, tracks, outputPath);
    }

    return false;
}

bool PlaylistManager::importPlaylist(const std::string& filePath) {
    if (!fs::exists(filePath)) {
        spdlog::error("PlaylistManager: File not found: {}", filePath);
        return false;
    }

    std::string ext = fs::path(filePath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    std::string playlistName = fs::path(filePath).stem().string();
    std::ifstream file(filePath);
    if (!file.is_open()) return false;

    std::vector<std::string> trackPaths;
    std::string line;

    if (ext == ".m3u" || ext == ".m3u8") {
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            trackPaths.push_back(line);
        }
    } else if (ext == ".pls") {
        while (std::getline(file, line)) {
            if (line.substr(0, 4) == "File") {
                auto pos = line.find('=');
                if (pos != std::string::npos) {
                    trackPaths.push_back(line.substr(pos + 1));
                }
            }
        }
    }

    int64_t newPlaylistId = createPlaylist(playlistName, "Imported from " + filePath);
    if (newPlaylistId > 0 && database_) {
        for (auto& path : trackPaths) {
            auto trackOpt = database_->getTrackByPath(path);
            if (trackOpt) {
                addTrack(newPlaylistId, trackOpt->id);
            }
        }
    }

    spdlog::info("PlaylistManager: Imported playlist '{}' with {} tracks", playlistName, trackPaths.size());
    return true;
}

int64_t PlaylistManager::createFolder(const std::string& name, int64_t parentId) {
    if (!database_ || !database_->isOpen()) {
        spdlog::error("PlaylistManager: No database connection for createFolder");
        return -1;
    }

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (!database_->executeWrite(
            "INSERT INTO playlists (name, description, is_smart, created_at, modified_at, parent_folder_id) "
            "VALUES (?, '', 0, ?, ?, ?)",
            {name, std::to_string(now), std::to_string(now), std::to_string(parentId)})) {
        spdlog::error("PlaylistManager: createFolder INSERT failed");
        return -1;
    }

    int64_t folderId = database_->getLastInsertRowId();
    if (folderId <= 0) {
        spdlog::error("PlaylistManager: createFolder last_insert_rowid returned {}", folderId);
        return -1;
    }
    spdlog::info("PlaylistManager: Created folder '{}' (id={}) under {}", name, folderId, parentId);
    return folderId;
}

bool PlaylistManager::movePlaylistToFolder(int64_t playlistId, int64_t folderId) {
    if (!database_ || !database_->isOpen()) {
        spdlog::error("PlaylistManager: No database connection for movePlaylistToFolder");
        return false;
    }

    bool ok = database_->executeWrite(
        "UPDATE playlists SET parent_folder_id = ? WHERE id = ?",
        {std::to_string(folderId), std::to_string(playlistId)}
    );

    spdlog::info("PlaylistManager: Moved playlist {} to folder {} (ok={})", playlistId, folderId, ok);
    return ok;
}

bool PlaylistManager::exportM3U(const Models::Playlist& playlist, const std::vector<Models::Track>& tracks,
                                  const std::string& outputPath, bool useUtf8) {
    std::ofstream file(outputPath);
    if (!file.is_open()) {
        spdlog::error("PlaylistManager: Cannot open file for writing: {}", outputPath);
        return false;
    }

    file << "#EXTM3U\n";
    if (useUtf8) {
        file << "#EXTENC:UTF-8\n";
    }
    file << "#PLAYLIST:" << playlist.name << "\n";

    for (const auto& track : tracks) {
        int durationSec = static_cast<int>(track.duration);
        file << "#EXTINF:" << durationSec << "," << track.artist << " - " << track.title << "\n";
        file << track.filePath << "\n";
    }

    spdlog::info("PlaylistManager: Exported M3U to {}", outputPath);
    return true;
}

bool PlaylistManager::exportPLS(const Models::Playlist& playlist, const std::vector<Models::Track>& tracks,
                                  const std::string& outputPath) {
    std::ofstream file(outputPath);
    if (!file.is_open()) {
        spdlog::error("PlaylistManager: Cannot open file for writing: {}", outputPath);
        return false;
    }

    file << "[playlist]\n";
    file << "NumberOfEntries=" << tracks.size() << "\n\n";

    for (size_t i = 0; i < tracks.size(); ++i) {
        int num = static_cast<int>(i + 1);
        file << "File" << num << "=" << tracks[i].filePath << "\n";
        file << "Title" << num << "=" << tracks[i].artist << " - " << tracks[i].title << "\n";
        file << "Length" << num << "=" << static_cast<int>(tracks[i].duration) << "\n\n";
    }

    file << "Version=2\n";

    spdlog::info("PlaylistManager: Exported PLS to {}", outputPath);
    return true;
}

namespace {
const char* sourceToString(Models::PlaylistSource s) {
    switch (s) {
        case Models::PlaylistSource::Rekordbox:  return "Rekordbox";
        case Models::PlaylistSource::VirtualDJ:  return "VirtualDJ";
        case Models::PlaylistSource::Serato:     return "Serato";
        case Models::PlaylistSource::Traktor:    return "Traktor";
        case Models::PlaylistSource::EngineDJ:   return "EngineDJ";
        case Models::PlaylistSource::Local:
        default:                                 return "Local";
    }
}

Models::PlaylistSource stringToSource(const std::string& s) {
    if (s == "Rekordbox") return Models::PlaylistSource::Rekordbox;
    if (s == "VirtualDJ") return Models::PlaylistSource::VirtualDJ;
    if (s == "Serato")    return Models::PlaylistSource::Serato;
    if (s == "Traktor")   return Models::PlaylistSource::Traktor;
    if (s == "EngineDJ")  return Models::PlaylistSource::EngineDJ;
    return Models::PlaylistSource::Local;
}
} // namespace

int64_t PlaylistManager::findPlaylistBySourceAndExternalId(Models::PlaylistSource source,
                                                           const std::string& externalId) {
    if (!database_ || !database_->isOpen()) return 0;
    int64_t found = 0;
    database_->executeRead(
        "SELECT id FROM playlists WHERE source = ? AND external_id = ? LIMIT 1",
        { sourceToString(source), externalId },
        [&](sqlite3_stmt* stmt) {
            found = sqlite3_column_int64(stmt, 0);
        });
    return found;
}

int64_t PlaylistManager::upsertExternalPlaylist(Models::PlaylistSource source,
                                                const std::string& externalId,
                                                const std::string& name,
                                                const std::vector<int64_t>& trackIds,
                                                const std::string& externalPath,
                                                int64_t parentFolderId) {
    if (!database_ || !database_->isOpen()) return 0;

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    int64_t id = findPlaylistBySourceAndExternalId(source, externalId);

    if (id > 0) {
        database_->executeWrite(
            "UPDATE playlists SET name = ?, modified_at = ?, external_path = ?, "
            "parent_folder_id = ? WHERE id = ?",
            { name, std::to_string(now), externalPath,
              std::to_string(parentFolderId), std::to_string(id) });
        database_->executeWrite(
            "DELETE FROM playlist_tracks WHERE playlist_id = ?",
            { std::to_string(id) });
    } else {
        database_->executeWrite(
            "INSERT INTO playlists (name, description, created_at, modified_at, "
            "parent_folder_id, source, external_id, external_path) "
            "VALUES (?, '', ?, ?, ?, ?, ?, ?)",
            { name, std::to_string(now), std::to_string(now),
              std::to_string(parentFolderId),
              sourceToString(source), externalId, externalPath });
        id = database_->getLastInsertRowId();
    }

    for (size_t i = 0; i < trackIds.size(); ++i) {
        database_->executeWrite(
            "INSERT INTO playlist_tracks (playlist_id, track_id, position) VALUES (?, ?, ?)",
            { std::to_string(id), std::to_string(trackIds[i]), std::to_string(i) });
    }

    return id;
}

std::vector<Models::Playlist> PlaylistManager::getPlaylistsBySource(Models::PlaylistSource source) {
    std::vector<Models::Playlist> out;
    if (!database_ || !database_->isOpen()) return out;

    database_->executeRead(
        "SELECT id, name, description, parent_folder_id, source, external_id, external_path "
        "FROM playlists WHERE source = ? ORDER BY name",
        { sourceToString(source) },
        [&](sqlite3_stmt* stmt) {
            Models::Playlist p;
            p.id = sqlite3_column_int64(stmt, 0);
            if (auto t = sqlite3_column_text(stmt, 1)) p.name = reinterpret_cast<const char*>(t);
            if (auto t = sqlite3_column_text(stmt, 2)) p.description = reinterpret_cast<const char*>(t);
            p.parentFolderId = sqlite3_column_int64(stmt, 3);
            if (auto t = sqlite3_column_text(stmt, 4))
                p.source = stringToSource(reinterpret_cast<const char*>(t));
            if (auto t = sqlite3_column_text(stmt, 5)) p.externalId = reinterpret_cast<const char*>(t);
            if (auto t = sqlite3_column_text(stmt, 6)) p.externalPath = reinterpret_cast<const char*>(t);
            out.push_back(std::move(p));
        });

    for (auto& p : out) {
        database_->executeRead(
            "SELECT track_id FROM playlist_tracks WHERE playlist_id = ? ORDER BY position",
            { std::to_string(p.id) },
            [&](sqlite3_stmt* stmt) {
                p.trackIds.push_back(sqlite3_column_int64(stmt, 0));
            });
    }
    return out;
}

bool PlaylistManager::deletePlaylistsBySource(Models::PlaylistSource source) {
    if (!database_ || !database_->isOpen()) return false;
    database_->executeWrite(
        "DELETE FROM playlist_tracks WHERE playlist_id IN "
        "(SELECT id FROM playlists WHERE source = ?)",
        { sourceToString(source) });
    database_->executeWrite(
        "DELETE FROM playlists WHERE source = ?",
        { sourceToString(source) });
    return true;
}

} // namespace BeatMate::Services::Library
