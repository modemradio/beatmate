#include "USBExporter.h"
#include <spdlog/spdlog.h>
#include <filesystem>
namespace fs = std::filesystem;
namespace BeatMate::Services::Export {
bool USBExporter::exportToUSB(const std::vector<Models::Track>& tracks, const std::string& drivePath, USBFormat format, USBProgressCallback callback) {
    if (!fs::exists(drivePath)) { spdlog::error("USBExporter: Drive not found: {}", drivePath); return false; }
    std::string musicDir = drivePath + "/BeatMate";
    fs::create_directories(musicDir);
    int total = static_cast<int>(tracks.size());
    for (int i = 0; i < total; ++i) {
        std::string dest = musicDir + "/" + fs::path(tracks[i].filePath).filename().string();
        try { fs::copy_file(tracks[i].filePath, dest, fs::copy_options::skip_existing); } catch (...) {}
        if (callback) callback(i + 1, total);
    }
    spdlog::info("USBExporter: Exported {} tracks to {}", total, drivePath);
    return true;
}
} // namespace BeatMate::Services::Export
