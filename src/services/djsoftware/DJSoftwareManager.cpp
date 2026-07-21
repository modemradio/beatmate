#include "DJSoftwareManager.h"
#include "DJSoftwareDetector.h"

#include <spdlog/spdlog.h>

namespace BeatMate::Services::DJSoftware {

DJSoftwareManager::DJSoftwareManager() {
}

std::vector<DJSoftwareInfo> DJSoftwareManager::getDetectedSoftware() {
    if (!scanned_) {
        refresh();
    }
    return detected_;
}

std::map<DJSoftwareType, SyncStatus> DJSoftwareManager::getSyncStatus() {
    return syncStatus_;
}

DJSoftwareInfo DJSoftwareManager::getSoftwareInfo(DJSoftwareType type) {
    if (!scanned_) refresh();
    for (const auto& info : detected_) {
        if (info.type == type) return info;
    }
    return DJSoftwareInfo{type, softwareTypeName(type), "", "", "", false, false};
}

bool DJSoftwareManager::isSoftwareInstalled(DJSoftwareType type) {
    auto info = getSoftwareInfo(type);
    return info.isInstalled;
}

void DJSoftwareManager::refresh() {
    DJSoftwareDetector detector;
    detected_ = detector.detect();
    scanned_ = true;

    for (const auto& info : detected_) {
        if (syncStatus_.find(info.type) == syncStatus_.end()) {
            syncStatus_[info.type] = SyncStatus::NotSynced;
        }
    }

    spdlog::info("DJSoftwareManager: Detected {} DJ software installations", detected_.size());
}

std::string DJSoftwareManager::softwareTypeName(DJSoftwareType type) {
    switch (type) {
        case DJSoftwareType::Rekordbox: return "Rekordbox";
        case DJSoftwareType::VirtualDJ: return "VirtualDJ";
        case DJSoftwareType::Serato: return "Serato DJ";
        case DJSoftwareType::Traktor: return "Traktor";
        case DJSoftwareType::EngineDJ: return "Engine DJ";
        case DJSoftwareType::DjayPro: return "djay Pro";
        default: return "Unknown";
    }
}

} // namespace BeatMate::Services::DJSoftware
