#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

#include "../../models/Track.h"

namespace BeatMate::Services::DJSoftware {

enum class DJSoftwareType {
    Rekordbox,
    VirtualDJ,
    Serato,
    Traktor,
    EngineDJ,
    DjayPro
};

struct DJSoftwareInfo {
    DJSoftwareType type;
    std::string name;
    std::string version;
    std::string installPath;
    std::string databasePath;
    bool isInstalled = false;
    bool isRunning = false;
};

enum class SyncStatus {
    NotSynced,
    InProgress,
    Synced,
    Error
};

class DJSoftwareManager {
public:
    DJSoftwareManager();
    ~DJSoftwareManager() = default;

    std::vector<DJSoftwareInfo> getDetectedSoftware();
    std::map<DJSoftwareType, SyncStatus> getSyncStatus();

    DJSoftwareInfo getSoftwareInfo(DJSoftwareType type);
    bool isSoftwareInstalled(DJSoftwareType type);

    void refresh();

    static std::string softwareTypeName(DJSoftwareType type);

private:
    std::vector<DJSoftwareInfo> detected_;
    std::map<DJSoftwareType, SyncStatus> syncStatus_;
    bool scanned_ = false;
};

} // namespace BeatMate::Services::DJSoftware
