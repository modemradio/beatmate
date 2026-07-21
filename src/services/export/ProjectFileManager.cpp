#include "ProjectFileManager.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <fstream>
using json = nlohmann::json;
namespace BeatMate::Services::Export {
bool ProjectFileManager::saveProject(const ProjectFile& project, const std::string& path) {
    json j;
    j["name"] = project.name; j["version"] = project.version;
    j["trackIds"] = project.trackIds; j["playlistIds"] = project.playlistIds;
    j["notes"] = project.notes;
    std::ofstream file(path);
    if (!file.is_open()) { spdlog::error("ProjectFileManager: Cannot write to {}", path); return false; }
    file << j.dump(2);
    spdlog::info("ProjectFileManager: Saved project '{}' to {}", project.name, path);
    return true;
}
ProjectFile ProjectFileManager::loadProject(const std::string& path) {
    ProjectFile project;
    std::ifstream file(path);
    if (!file.is_open()) { spdlog::error("ProjectFileManager: Cannot read {}", path); return project; }
    try {
        json j = json::parse(file);
        project.name = j.value("name", ""); project.version = j.value("version", "1.0");
        project.trackIds = j.value("trackIds", std::vector<int64_t>{});
        project.playlistIds = j.value("playlistIds", std::vector<int64_t>{});
        project.notes = j.value("notes", "");
    } catch (const std::exception& e) { spdlog::error("ProjectFileManager: Parse error: {}", e.what()); }
    return project;
}
} // namespace BeatMate::Services::Export
