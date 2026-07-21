#pragma once

#include <string>
#include <vector>

#include "DJSoftwareManager.h"

namespace BeatMate::Services::DJSoftware {

class DJSoftwareDetector {
public:
    DJSoftwareDetector() = default;
    ~DJSoftwareDetector() = default;

    std::vector<DJSoftwareInfo> detect();

private:
    DJSoftwareInfo detectRekordbox();
    DJSoftwareInfo detectVirtualDJ();
    DJSoftwareInfo detectSerato();
    DJSoftwareInfo detectTraktor();
    DJSoftwareInfo detectEngineDJ();
    DJSoftwareInfo detectDjayPro();

    bool directoryExists(const std::string& path) const;
    bool fileExists(const std::string& path) const;
    std::string getAppDataPath() const;
    std::string getProgramFilesPath() const;
    std::string getRegistryValue(const std::string& key, const std::string& valueName) const;
};

} // namespace BeatMate::Services::DJSoftware
