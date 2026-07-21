#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models::Settings {

struct BackupEntry {
    std::string path;
    std::string name;
    int64_t createdAt = 0;          // unix timestamp
    int64_t fileSize = 0;           // bytes
    std::string version;            // app version at time of backup
    bool isAutoBackup = false;

    BackupEntry() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BackupEntry,
        path, name, createdAt, fileSize, version, isAutoBackup
    )
};

struct BackupSettings {
    bool autoBackup = true;
    int backupIntervalHours = 24;       // hours between auto-backups
    int maxBackups = 10;                // max number of backups to keep
    std::string backupPath;             // backup directory

    bool backupDatabase = true;
    bool backupSettings = true;
    bool backupPlaylists = true;
    bool backupCuePoints = true;
    bool backupAnalysisData = true;
    bool backupControllerProfiles = true;
    bool backupEventPlans = true;

    bool compressBackups = true;
    std::string compressionFormat = "zip"; // "zip", "7z", "tar.gz"

    bool cloudBackup = false;
    std::string cloudProvider;          // "dropbox", "google_drive", "onedrive"
    std::string cloudPath;
    int64_t lastCloudBackup = 0;

    std::vector<BackupEntry> backupHistory;

    int64_t lastBackup = 0;             // unix timestamp of last backup
    int64_t lastRestore = 0;            // unix timestamp of last restore

    bool notifyOnBackup = false;
    bool notifyOnFailure = true;

    BackupSettings() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BackupSettings,
        autoBackup, backupIntervalHours, maxBackups, backupPath,
        backupDatabase, backupSettings, backupPlaylists, backupCuePoints,
        backupAnalysisData, backupControllerProfiles, backupEventPlans,
        compressBackups, compressionFormat,
        cloudBackup, cloudProvider, cloudPath, lastCloudBackup,
        backupHistory,
        lastBackup, lastRestore,
        notifyOnBackup, notifyOnFailure
    )
};

} // namespace BeatMate::Models::Settings
