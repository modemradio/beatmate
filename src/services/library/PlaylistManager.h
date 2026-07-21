#pragma once
#include <cstdint>

#include <string>
#include <vector>
#include <memory>
#include <optional>

#include "../../models/Playlist.h"
#include "../../models/Track.h"
#include "../../models/PlaylistTrack.h"

namespace BeatMate::Services::Library {

class TrackDatabase;

enum class PlaylistExportFormat {
    M3U,
    M3U8,
    PLS
};

class PlaylistManager {
public:
    explicit PlaylistManager(std::shared_ptr<TrackDatabase> database);
    ~PlaylistManager() = default;

    int64_t createPlaylist(const std::string& name, const std::string& description = "");
    bool deletePlaylist(int64_t playlistId);
    bool renamePlaylist(int64_t playlistId, const std::string& name);
    std::optional<Models::Playlist> getPlaylist(int64_t playlistId);
    std::vector<Models::Playlist> getAllPlaylists();

    bool addTrack(int64_t playlistId, int64_t trackId);
    bool removeTrack(int64_t playlistId, int64_t trackId);
    bool reorderTracks(int64_t playlistId, const std::vector<int64_t>& trackIds);
    std::vector<Models::Track> getPlaylistTracks(int64_t playlistId);
    int getTrackCount(int64_t playlistId);

    bool exportPlaylist(int64_t playlistId, const std::string& outputPath, PlaylistExportFormat format);
    bool importPlaylist(const std::string& filePath);

    int64_t createFolder(const std::string& name, int64_t parentId = -1);
    bool movePlaylistToFolder(int64_t playlistId, int64_t folderId);

    int64_t upsertExternalPlaylist(Models::PlaylistSource source,
                                   const std::string& externalId,
                                   const std::string& name,
                                   const std::vector<int64_t>& trackIds,
                                   const std::string& externalPath = "",
                                   int64_t parentFolderId = -1);
    int64_t findPlaylistBySourceAndExternalId(Models::PlaylistSource source,
                                              const std::string& externalId);
    std::vector<Models::Playlist> getPlaylistsBySource(Models::PlaylistSource source);
    bool deletePlaylistsBySource(Models::PlaylistSource source);

private:
    bool exportM3U(const Models::Playlist& playlist, const std::vector<Models::Track>& tracks,
                   const std::string& outputPath, bool useUtf8);
    bool exportPLS(const Models::Playlist& playlist, const std::vector<Models::Track>& tracks,
                   const std::string& outputPath);

    std::shared_ptr<TrackDatabase> database_;
};

}
