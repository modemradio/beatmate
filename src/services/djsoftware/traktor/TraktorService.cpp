#include "TraktorService.h"
#include "TraktorCollectionParser.h"

#include <spdlog/spdlog.h>
#include <filesystem>
#include <cstdlib>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#endif

namespace fs = std::filesystem;

namespace BeatMate::Services::Traktor {

TraktorService::TraktorService()
    : parser_(std::make_unique<TraktorCollectionParser>()) {
}

TraktorService::~TraktorService() = default;

bool TraktorService::initialize() {
    std::string collectionPath = findCollectionPath();
    if (collectionPath.empty()) {
        spdlog::warn("TraktorService: collection.nml not found");
        return false;
    }

    initialized_ = true;
    spdlog::info("TraktorService: Initialized with {}", collectionPath);
    return true;
}

bool TraktorService::isAvailable() const {
    return initialized_;
}

std::vector<Models::TraktorTrack> TraktorService::readCollection() {
    if (!initialized_) return {};

    std::string path = findCollectionPath();
    if (path.empty()) return {};

    auto tracks = parser_->parse(path);
    spdlog::info("TraktorService: Read {} tracks from Traktor", tracks.size());
    return tracks;
}

std::string TraktorService::findCollectionPath() const {
#ifdef _WIN32
    char localAppData[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData) != S_OK) {
        return "";
    }

    std::vector<std::string> paths = {
        std::string(localAppData) + "/Native Instruments/Traktor Pro 4/collection.nml",
        std::string(localAppData) + "/Native Instruments/Traktor Pro 3/collection.nml",
        std::string(localAppData) + "/Native Instruments/Traktor Pro 2/collection.nml",
    };

    for (const auto& p : paths) {
        if (fs::exists(p)) return p;
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        std::string h(home);
        std::vector<std::string> paths = {
            h + "/Documents/Native Instruments/Traktor Pro 4/collection.nml",
            h + "/Documents/Native Instruments/Traktor Pro 3/collection.nml",
            h + "/Documents/Native Instruments/Traktor Pro 2/collection.nml",
        };
        for (const auto& p : paths) {
            std::error_code ec;
            if (fs::exists(p, ec)) return p;
        }
    }
#endif
    return "";
}

}
