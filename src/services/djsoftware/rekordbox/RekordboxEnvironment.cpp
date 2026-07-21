#include "RekordboxEnvironment.h"

#include <spdlog/spdlog.h>
#include <filesystem>
#include <cstdlib>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#endif

namespace fs = std::filesystem;

namespace BeatMate::Services::Rekordbox {

bool RekordboxEnvironment::isInstalled() const {
    return !findDatabasePath().empty() || !findXmlPath().empty();
}

std::string RekordboxEnvironment::findDatabasePath() const {
    std::vector<std::string> paths;

    if (std::string appData = getAppDataPath(); !appData.empty()) {
        for (const char* sub : { "/Pioneer/rekordbox7/master.db",
                                  "/Pioneer/rekordbox/master.db",
                                  "/Pioneer/rekordbox6/master.db" }) {
            paths.push_back(appData + sub);
        }
    }

#ifdef _WIN32
    char localAppData[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData) == S_OK) {
        std::string la(localAppData);
        paths.push_back(la + "/Pioneer/rekordbox7/master.db");
        paths.push_back(la + "/Pioneer/rekordbox/master.db");
    }
#endif

    for (const auto& p : paths) {
        std::error_code ec;
        if (fs::exists(p, ec)) {
            spdlog::info("RekordboxEnvironment: Found database at {}", p);
            return p;
        }
    }
    spdlog::warn("RekordboxEnvironment: master.db introuvable (testes : {})", paths.size());
    return "";
}

std::string RekordboxEnvironment::findXmlPath() const {
    std::string appData = getAppDataPath();

    std::vector<std::string> paths;

    if (!appData.empty()) {
        paths.push_back(appData + "/Pioneer/rekordbox7/rekordbox.xml");
        paths.push_back(appData + "/Pioneer/rekordbox/rekordbox.xml");
        paths.push_back(appData + "/Pioneer/rekordbox6/rekordbox.xml");
    }

#ifdef _WIN32
    char docs[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, docs) == S_OK) {
        std::string docsPath(docs);
        paths.push_back(docsPath + "/rekordbox.xml");
        paths.push_back(docsPath + "/Pioneer/rekordbox.xml");
        paths.push_back(docsPath + "/rekordbox/rekordbox.xml");
    }

    char desktop[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_DESKTOP, NULL, 0, desktop) == S_OK) {
        paths.push_back(std::string(desktop) + "/rekordbox.xml");
    }

    char profile[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, profile) == S_OK) {
        paths.push_back(std::string(profile) + "/Downloads/rekordbox.xml");
    }
#endif

    for (const auto& p : paths) {
        try {
            if (fs::exists(p)) {
                spdlog::info("RekordboxEnvironment: Found XML at {}", p);
                return p;
            }
        } catch (...) {}
    }

    return "";
}

std::string RekordboxEnvironment::getInstallPath() const {
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

std::string RekordboxEnvironment::getVersion() const {
    return "6.0";
}

int RekordboxEnvironment::getMajorVersion() const {
    std::string ver = getVersion();
    try {
        return std::stoi(ver);
    } catch (...) {
        return 6;
    }
}

std::string RekordboxEnvironment::getAppDataPath() const {
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

}
