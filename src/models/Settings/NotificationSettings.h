#pragma once

#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models::Settings {

struct NotificationSettings {
    bool showToasts = true;
    int toastDurationMs = 5000;         // how long toasts are shown
    std::string toastPosition = "bottom-right"; // "top-left", "top-right", "bottom-left", "bottom-right"
    int maxVisibleToasts = 3;

    bool soundAlerts = false;
    float soundVolume = 0.5f;           // 0-1
    std::string soundFile;              // custom sound file path

    bool notifyAnalysisComplete = true;
    bool notifyImportComplete = true;
    bool notifyExportComplete = true;
    bool notifySyncComplete = true;
    bool notifyErrors = true;
    bool notifyWarnings = true;
    bool notifyUpdates = true;
    bool notifyLicenseExpiry = true;

    bool syncNotifications = true;
    bool notifySyncConflicts = true;
    bool notifySyncErrors = true;

    bool systemTrayNotifications = true;
    bool systemTrayBadge = true;

    bool doNotDisturb = false;
    std::string dndStartTime = "22:00";
    std::string dndEndTime = "08:00";
    bool dndAllowErrors = true;         // show errors even in DND mode

    bool keepNotificationHistory = true;
    int maxNotificationHistory = 500;

    NotificationSettings() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(NotificationSettings,
        showToasts, toastDurationMs, toastPosition, maxVisibleToasts,
        soundAlerts, soundVolume, soundFile,
        notifyAnalysisComplete, notifyImportComplete, notifyExportComplete,
        notifySyncComplete, notifyErrors, notifyWarnings, notifyUpdates,
        notifyLicenseExpiry,
        syncNotifications, notifySyncConflicts, notifySyncErrors,
        systemTrayNotifications, systemTrayBadge,
        doNotDisturb, dndStartTime, dndEndTime, dndAllowErrors,
        keepNotificationHistory, maxNotificationHistory
    )
};

} // namespace BeatMate::Models::Settings
