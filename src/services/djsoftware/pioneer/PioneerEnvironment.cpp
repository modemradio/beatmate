#include "PioneerEnvironment.h"

#include <spdlog/spdlog.h>
#include <filesystem>
#include <cstdlib>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#endif

namespace fs = std::filesystem;

namespace BeatMate::Services::PioneerDJ {

bool PioneerEnvironment::isInstalled() const {
    return !findDatabasePath().empty() || !findXmlPath().empty();
}

std::string PioneerEnvironment::findDatabasePath() const {
    std::string appData = getAppDataPath();
    if (appData.empty()) return "";

    // Rekordbox 6+ database
    std::vector<std::string> paths = {
        appData + "/Pioneer/rekordbox/master.db",
        appData + "/Pioneer/rekordbox6/master.db",
    };

    for (const auto& p : paths) {
        if (fs::exists(p)) {
            spdlog::debug("PioneerEnvironment: Found database at {}", p);
            return p;
        }
    }

    return "";
}

std::string PioneerEnvironment::findXmlPath() const {
    std::string appData = getAppDataPath();
    if (appData.empty()) return "";

    std::vector<std::string> paths = {
        appData + "/Pioneer/rekordbox/rekordbox.xml",
        appData + "/Pioneer/rekordbox6/rekordbox.xml",
    };

    for (const auto& p : paths) {
        if (fs::exists(p)) {
            return p;
        }
    }

    return "";
}

std::string PioneerEnvironment::getInstallPath() const {
#ifdef _WIN32
    char programFiles[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PROGRAM_FILES, NULL, 0, programFiles) == S_OK) {
        std::string path = std::string(programFiles) + "/Pioneer/rekordbox 6";
        if (fs::exists(path)) return path;
        path = std::string(programFiles) + "/Pioneer/rekordbox";
        if (fs::exists(path)) return path;
    }
#endif
    return "";
}

std::string PioneerEnvironment::getVersion() const {
    return "6.0";
}

int PioneerEnvironment::getMajorVersion() const {
    std::string ver = getVersion();
    try {
        return std::stoi(ver);
    } catch (...) {
        return 6;
    }
}

std::string PioneerEnvironment::getAppDataPath() const {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path) == S_OK) {
        return std::string(path);
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"))
        return std::string(home) + "/Library";
#endif
    return "";
}

} // namespace BeatMate::Services::PioneerDJ
