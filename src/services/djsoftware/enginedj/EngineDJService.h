#pragma once

#include <string>
#include <vector>

#include "../../../models/Track.h"

namespace BeatMate::Services::EngineDJ {

struct EngineDJPlaylistInfo {
    int64_t engineId = 0;            // Playlist.id in m.db
    int64_t parentEngineId = 0;      // Playlist.parentListId (0 = root)
    std::string name;
    std::vector<std::string> trackPaths; // absolute or relative paths resolved
};

class EngineDJService {
public:
    EngineDJService() = default;
    ~EngineDJService() = default;

    bool initialize();
    bool isAvailable() const;
    std::vector<Models::Track> readDatabase();
    std::string findDatabasePath() const;

    // Read playlists from m.db. Builds a flat list with parent linkage; callers
    std::vector<EngineDJPlaylistInfo> readPlaylists();

private:
    bool initialized_ = false;
    std::string dbPath_;
};

} // namespace BeatMate::Services::EngineDJ
