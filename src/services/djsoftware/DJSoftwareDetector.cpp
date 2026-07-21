#include "DJSoftwareDetector.h"

#include <spdlog/spdlog.h>
#include <filesystem>
#include <cstdlib>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#endif

namespace fs = std::filesystem;

namespace BeatMate::Services::DJSoftware {

std::vector<DJSoftwareInfo> DJSoftwareDetector::detect() {
    std::vector<DJSoftwareInfo> detected;

    auto rb = detectRekordbox();
    if (rb.isInstalled) detected.push_back(rb);

    auto vdj = detectVirtualDJ();
    if (vdj.isInstalled) detected.push_back(vdj);

    auto serato = detectSerato();
    if (serato.isInstalled) detected.push_back(serato);

    auto traktor = detectTraktor();
    if (traktor.isInstalled) detected.push_back(traktor);

    auto engine = detectEngineDJ();
    if (engine.isInstalled) detected.push_back(engine);

    auto djay = detectDjayPro();
    if (djay.isInstalled) detected.push_back(djay);

    spdlog::info("DJSoftwareDetector: Detected {} DJ software installations", detected.size());
    return detected;
}

DJSoftwareInfo DJSoftwareDetector::detectRekordbox() {
    DJSoftwareInfo info;
    info.type = DJSoftwareType::Rekordbox;
    info.name = "Rekordbox";

    std::string appData = getAppDataPath();

    std::vector<std::string> possiblePaths = {
        appData + "/Pioneer/rekordbox",
        appData + "/Pioneer/rekordbox6",
        getProgramFilesPath() + "/Pioneer/rekordbox 6",
        getProgramFilesPath() + "/Pioneer/rekordbox",
    };

#if defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        possiblePaths.insert(possiblePaths.begin(),
                             std::string(home) + "/Library/Pioneer/rekordbox");
    }
#endif

    for (const auto& path : possiblePaths) {
        if (directoryExists(path)) {
            info.installPath = path;
            info.isInstalled = true;

            std::string dbPath = path + "/master.db";
            if (fileExists(dbPath)) {
                info.databasePath = dbPath;
            }
            break;
        }
    }

    if (info.isInstalled) {
        spdlog::info("DJSoftwareDetector: Found Rekordbox at {}", info.installPath);
    }

    return info;
}

DJSoftwareInfo DJSoftwareDetector::detectVirtualDJ() {
    DJSoftwareInfo info;
    info.type = DJSoftwareType::VirtualDJ;
    info.name = "VirtualDJ";

    std::string appData = getAppDataPath();
    std::string userDocuments;
    std::string localAppData;

#ifdef _WIN32
    char path[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, path) == S_OK) {
        userDocuments = path;
    }
    char lpath[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, lpath) == S_OK) {
        localAppData = lpath;
    }
#endif

    std::vector<std::string> possiblePaths = {
        localAppData + "/VirtualDJ",
        userDocuments + "/VirtualDJ",
        appData + "/VirtualDJ",
        getProgramFilesPath() + "/VirtualDJ",
    };

#if defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        std::string h(home);
        possiblePaths.insert(possiblePaths.begin(), {
            h + "/Library/Application Support/VirtualDJ",
            h + "/Documents/VirtualDJ",
        });
    }
#endif

    for (const auto& path : possiblePaths) {
        if (!directoryExists(path)) continue;
        if (!info.isInstalled) {
            info.installPath = path;
            info.isInstalled = true;
        }
        std::string dbPath = path + "/database.xml";
        if (fileExists(dbPath)) {
            info.installPath = path;
            info.databasePath = dbPath;
            break;
        }
    }

    if (info.isInstalled) {
        spdlog::info("DJSoftwareDetector: Found VirtualDJ at {} (db='{}')",
                     info.installPath, info.databasePath);
    }

    return info;
}

DJSoftwareInfo DJSoftwareDetector::detectSerato() {
    DJSoftwareInfo info;
    info.type = DJSoftwareType::Serato;
    info.name = "Serato DJ";

    std::string appData = getAppDataPath();
    std::string localAppData;

#ifdef _WIN32
    char path[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path) == S_OK) {
        localAppData = path;
    }
#endif

    std::vector<std::string> possiblePaths = {
        localAppData + "/Serato",
        appData + "/Serato",
    };

#if defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        possiblePaths.insert(possiblePaths.begin(),
                             std::string(home) + "/Music/_Serato_");
    }
#endif

    for (const auto& p : possiblePaths) {
        if (directoryExists(p)) {
            info.installPath = p;
            info.isInstalled = true;
            info.databasePath = p;
            break;
        }
    }

    if (info.isInstalled) {
        spdlog::info("DJSoftwareDetector: Found Serato at {}", info.installPath);
    }

    return info;
}

DJSoftwareInfo DJSoftwareDetector::detectTraktor() {
    DJSoftwareInfo info;
    info.type = DJSoftwareType::Traktor;
    info.name = "Traktor";

    std::string appData = getAppDataPath();
    std::string localAppData;

#ifdef _WIN32
    char path[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path) == S_OK) {
        localAppData = path;
    }
#endif

    std::vector<std::string> possiblePaths = {
        localAppData + "/Native Instruments/Traktor Pro 3",
        localAppData + "/Native Instruments/Traktor Pro 4",
        appData + "/Native Instruments/Traktor Pro 3",
    };

#if defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        std::string h(home);
        possiblePaths.insert(possiblePaths.begin(), {
            h + "/Documents/Native Instruments/Traktor Pro 4",
            h + "/Documents/Native Instruments/Traktor Pro 3",
        });
    }
#endif

    for (const auto& p : possiblePaths) {
        if (directoryExists(p)) {
            info.installPath = p;
            info.isInstalled = true;

            std::string collectionPath = p + "/collection.nml";
            if (fileExists(collectionPath)) {
                info.databasePath = collectionPath;
            }
            break;
        }
    }

    if (info.isInstalled) {
        spdlog::info("DJSoftwareDetector: Found Traktor at {}", info.installPath);
    }

    return info;
}

DJSoftwareInfo DJSoftwareDetector::detectEngineDJ() {
    DJSoftwareInfo info;
    info.type = DJSoftwareType::EngineDJ;
    info.name = "Engine DJ";

    std::string appData = getAppDataPath();

    std::vector<std::string> possiblePaths = {
        appData + "/Engine DJ",
        appData + "/Denon DJ/Engine Library",
    };

#if defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        std::string h(home);
        possiblePaths.insert(possiblePaths.begin(), {
            h + "/Music/Engine Library",
            h + "/Music/Engine DJ",
        });
    }
#endif

    for (const auto& p : possiblePaths) {
        if (directoryExists(p)) {
            info.installPath = p;
            info.isInstalled = true;

            std::string dbPath = p + "/m.db";
            if (fileExists(dbPath)) {
                info.databasePath = dbPath;
            }
            break;
        }
    }

    if (info.isInstalled) {
        spdlog::info("DJSoftwareDetector: Found Engine DJ at {}", info.installPath);
    }

    return info;
}

DJSoftwareInfo DJSoftwareDetector::detectDjayPro() {
    DJSoftwareInfo info;
    info.type = DJSoftwareType::DjayPro;
    info.name = "djay Pro";

    std::string appData = getAppDataPath();

    std::vector<std::string> possiblePaths = {
        appData + "/Algoriddim/djay",
        getProgramFilesPath() + "/Algoriddim/djay",
    };

#if defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        std::string h(home);
        possiblePaths.insert(possiblePaths.begin(), {
            h + "/Library/Containers/com.algoriddim.djay-pro-mac/Data/Library/Application Support/djay",
            h + "/Music/djay",
        });
    }
#endif

    for (const auto& p : possiblePaths) {
        if (directoryExists(p)) {
            info.installPath = p;
            info.isInstalled = true;
            info.databasePath = p;
            break;
        }
    }

    return info;
}

bool DJSoftwareDetector::directoryExists(const std::string& path) const {
    try {
        return fs::exists(path) && fs::is_directory(path);
    } catch (...) {
        return false;
    }
}

bool DJSoftwareDetector::fileExists(const std::string& path) const {
    try {
        return fs::exists(path) && fs::is_regular_file(path);
    } catch (...) {
        return false;
    }
}

std::string DJSoftwareDetector::getAppDataPath() const {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path) == S_OK) {
        return std::string(path);
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"))
        return std::string(home) + "/Library/Application Support";
#endif
    return "";
}

std::string DJSoftwareDetector::getProgramFilesPath() const {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PROGRAM_FILES, NULL, 0, path) == S_OK) {
        return std::string(path);
    }
#elif defined(__APPLE__)
    return "/Applications";
#endif
    return "";
}

std::string DJSoftwareDetector::getRegistryValue(const std::string& key, const std::string& valueName) const {
#ifdef _WIN32
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buffer[512];
        DWORD size = sizeof(buffer);
        if (RegQueryValueExA(hKey, valueName.c_str(), NULL, NULL,
                             reinterpret_cast<LPBYTE>(buffer), &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return std::string(buffer);
        }
        RegCloseKey(hKey);
    }
#endif
    return "";
}

} // namespace BeatMate::Services::DJSoftware
