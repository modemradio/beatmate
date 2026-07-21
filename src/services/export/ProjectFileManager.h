#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../../models/Track.h"
#include "../../models/Playlist.h"
namespace BeatMate::Services::Export {
struct ProjectFile { std::string name; std::string version = "1.0"; std::vector<int64_t> trackIds; std::vector<int64_t> playlistIds; std::string notes; };
class ProjectFileManager {
public:
    ProjectFileManager() = default;
    bool saveProject(const ProjectFile& project, const std::string& path);
    ProjectFile loadProject(const std::string& path);
};
} // namespace BeatMate::Services::Export
