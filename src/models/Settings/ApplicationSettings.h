#pragma once

#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models::Settings {

struct ApplicationSettings {
    std::string language = "en";            // ISO 639-1 code
    std::string region = "US";              // ISO 3166-1 alpha-2
    std::string dateFormat = "YYYY-MM-DD";
    std::string timeFormat = "24h";         // "12h" or "24h"

    std::string theme = "dark";             // "dark", "light", "system"
    std::string accentColor = "#6366F1";    // hex color

    bool autoSave = true;
    int autoSaveIntervalMinutes = 5;

    bool checkUpdates = true;
    bool autoInstallUpdates = false;
    std::string updateChannel = "stable";   // "stable", "beta", "nightly"

    bool restoreLastSession = true;
    bool showWelcomeScreen = true;
    bool minimizeToTray = false;
    bool startMinimized = false;
    bool launchAtStartup = false;

    int windowX = -1;
    int windowY = -1;
    int windowWidth = 1280;
    int windowHeight = 800;
    bool windowMaximized = false;

    std::string logLevel = "info";          // "debug", "info", "warning", "error"
    bool logToFile = true;
    std::string logPath;
    int maxLogFileSizeMB = 50;

    std::string databasePath;
    std::string cachePath;
    std::string tempPath;
    std::string pluginPath;

    bool confirmExit = true;
    bool showStatusBar = true;
    bool enableNotifications = true;
    int recentFilesMax = 20;
    std::string defaultExportPath;

    ApplicationSettings() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ApplicationSettings,
        language, region, dateFormat, timeFormat,
        theme, accentColor,
        autoSave, autoSaveIntervalMinutes,
        checkUpdates, autoInstallUpdates, updateChannel,
        restoreLastSession, showWelcomeScreen, minimizeToTray, startMinimized, launchAtStartup,
        windowX, windowY, windowWidth, windowHeight, windowMaximized,
        logLevel, logToFile, logPath, maxLogFileSizeMB,
        databasePath, cachePath, tempPath, pluginPath,
        confirmExit, showStatusBar, enableNotifications, recentFilesMax,
        defaultExportPath
    )
};

}
