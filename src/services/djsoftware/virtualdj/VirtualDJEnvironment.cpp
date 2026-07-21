#include "VirtualDJEnvironment.h"

#include <spdlog/spdlog.h>
#include <filesystem>
#include <cstdlib>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#endif

namespace fs = std::filesystem;

namespace BeatMate::Services::VirtualDJ {

bool VirtualDJEnvironment::isInstalled() const {
    return !findInstallation().empty();
}

std::string VirtualDJEnvironment::findInstallation() const {
    std::string docs = getDocumentsPath();
    if (!docs.empty()) {
        std::string vdjPath = docs + "/VirtualDJ";
        if (fs::exists(vdjPath)) return vdjPath;
    }

#if defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        std::string appSupport = std::string(home) + "/Library/Application Support/VirtualDJ";
        if (fs::exists(appSupport)) return appSupport;
    }
#endif

#ifdef _WIN32
    char programFiles[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PROGRAM_FILES, NULL, 0, programFiles) == S_OK) {
        std::string path = std::string(programFiles) + "/VirtualDJ";
        if (fs::exists(path)) return path;
    }
#endif

    return "";
}

std::string VirtualDJEnvironment::getDatabasePath() const {
    std::string install = findInstallation();
    if (install.empty()) return "";

    std::string xmlPath = install + "/database.xml";
    if (fs::exists(xmlPath)) return xmlPath;

    std::string vdbPath = install + "/database.vdb";
    if (fs::exists(vdbPath)) return vdbPath;

    return "";
}

std::string VirtualDJEnvironment::getVersion() const {
    return "";
}

std::string VirtualDJEnvironment::getDocumentsPath() const {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, path) == S_OK) {
        return std::string(path);
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"))
        return std::string(home) + "/Documents";
#endif
    return "";
}

} // namespace BeatMate::Services::VirtualDJ
