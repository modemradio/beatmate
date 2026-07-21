#include "DjayProService.h"

#include <spdlog/spdlog.h>
#include <filesystem>
#include <cstdlib>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#endif

namespace fs = std::filesystem;

namespace BeatMate::Services::DjayPro {

bool DjayProService::initialize() {
    std::string dbPath = findDatabasePath();
    if (dbPath.empty()) {
        spdlog::warn("DjayProService: djay Pro database not found");
        return false;
    }
    initialized_ = true;
    spdlog::info("DjayProService: Initialized");
    return true;
}

bool DjayProService::isAvailable() const {
    return initialized_;
}

std::vector<Models::Track> DjayProService::readCollection() {
    std::vector<Models::Track> tracks;
    if (!initialized_) return tracks;

    // djay Pro uses CoreData on macOS and a proprietary format on Windows
    spdlog::info("DjayProService: Reading collection");

    return tracks;
}

std::string DjayProService::findDatabasePath() const {
#ifdef _WIN32
    char appData[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appData) == S_OK) {
        std::string path = std::string(appData) + "/Algoriddim/djay";
        if (fs::exists(path)) return path;
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        std::string h(home);
        for (const std::string& p : {
            h + "/Library/Containers/com.algoriddim.djay-pro-mac/Data/Library/Application Support/djay Pro",
            h + "/Music/djay",
        }) {
            std::error_code ec;
            if (fs::exists(p, ec)) return p;
        }
    }
#endif
    return "";
}

} // namespace BeatMate::Services::DjayPro
