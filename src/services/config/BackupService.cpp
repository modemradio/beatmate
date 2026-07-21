#include "BackupService.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <juce_core/juce_core.h>

namespace fs = std::filesystem;

namespace BeatMate::Services::Config {

static std::string defaultBackupDir() {
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("BeatMate").getChildFile("backups");
    return dir.getFullPathName().toStdString();
}

static std::string defaultDbPath() {
    auto file = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("BeatMate").getChildFile("beatmate.db");
    return file.getFullPathName().toStdString();
}

static std::string nowTimestamp() {
    return juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S").toStdString();
}

BackupService::BackupService() {
    // Sensible defaults — if the caller never calls setBackupDir / does not
    if (backupDir_.empty()) backupDir_ = defaultBackupDir();
    if (dbPath_.empty())    dbPath_    = defaultDbPath();
}

BackupService::~BackupService() { stopAutoBackup(); }

bool BackupService::createBackup(const std::string& dbPath) {
    // Cooldown 30 s : a renegade caller (observed : the JUCE timer fires too
    static int64_t s_lastBackupMs = 0;
    const int64_t nowMs = static_cast<int64_t>(juce::Time::getMillisecondCounter());
    if (s_lastBackupMs != 0 && (nowMs - s_lastBackupMs) < 30 * 1000) {
        return true; // Silently skip — previous backup is still fresh.
    }
    s_lastBackupMs = nowMs;

    std::string src = dbPath.empty() ? dbPath_ : dbPath;
    if (src.empty() || !fs::exists(src)) {
        spdlog::warn("BackupService: source DB not found at '{}', skipping backup", src);
        return false;
    }
    try {
        fs::create_directories(backupDir_);
    } catch (const std::exception& e) {
        spdlog::error("BackupService: cannot create backup dir '{}': {}", backupDir_, e.what());
        return false;
    }

    const std::string stamp = nowTimestamp();
    std::string dest = backupDir_ + "/backup_" + stamp + "_" + fs::path(src).filename().string();
    try {
        // overwrite_existing guards against two scheduler ticks hitting the
        fs::copy_file(src, dest, fs::copy_options::overwrite_existing);
    } catch (const std::exception& e) {
        spdlog::error("BackupService: copy failed: {}", e.what());
        return false;
    }

    // Also copy companion JSON files when present so a restore brings back
    auto companionDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                            .getChildFile("BeatMate");
    for (const char* fname : { "events.json", "appsettings.json",
                               "filter_presets.json", "theme.json", "settings.json", "shortcuts.json" }) {
        auto companion = companionDir.getChildFile(fname);
        if (companion.existsAsFile()) {
            try {
                fs::copy_file(companion.getFullPathName().toStdString(),
                               backupDir_ + "/backup_" + stamp + "_" + fname,
                               fs::copy_options::overwrite_existing);
            } catch (...) { /* non-fatal — DB backup is the critical part */ }
        }
    }

    pruneOldBackups();
    spdlog::info("BackupService: Created backup {}", dest);
    return true;
}

bool BackupService::restoreBackup(const std::string& backupPath) {
    if (!fs::exists(backupPath) || dbPath_.empty()) return false;
    try { fs::copy_file(backupPath, dbPath_, fs::copy_options::overwrite_existing); } catch (...) { return false; }

    // Restore the companion files sharing the same timestamp (agenda,
    const std::string fileName = fs::path(backupPath).filename().string(); // backup_<stamp>_beatmate.db
    const std::string dbName   = fs::path(dbPath_).filename().string();
    if (fileName.size() > dbName.size()) {
        const std::string prefix = fileName.substr(0, fileName.size() - dbName.size()); // backup_<stamp>_
        auto companionDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                .getChildFile("BeatMate");
        for (const char* fname : { "events.json", "appsettings.json",
                                   "filter_presets.json", "theme.json", "settings.json", "shortcuts.json" }) {
            const std::string src = backupDir_ + "/" + prefix + fname;
            if (fs::exists(src)) {
                try {
                    fs::copy_file(src, companionDir.getChildFile(fname).getFullPathName().toStdString(),
                                  fs::copy_options::overwrite_existing);
                } catch (...) { /* non-fatal — DB restore is the critical part */ }
            }
        }
    }

    spdlog::info("BackupService: Restored from {}", backupPath);
    return true;
}

std::vector<std::string> BackupService::listBackups() const {
    std::vector<std::string> backups;
    if (!fs::exists(backupDir_)) return backups;
    for (const auto& entry : fs::directory_iterator(backupDir_)) {
        if (entry.is_regular_file()) backups.push_back(entry.path().string());
    }
    std::sort(backups.begin(), backups.end());
    return backups;
}

void BackupService::startAutoBackup(int intervalMinutes) { startTimer(intervalMinutes * 60 * 1000); }
void BackupService::stopAutoBackup() { stopTimer(); }
void BackupService::timerCallback() { createBackup(); }

void BackupService::pruneOldBackups() {
    // A snapshot is a group of files sharing the same backup_<stamp>_ prefix
    auto backups = listBackups();
    std::vector<std::string> stamps; // sorted, oldest first (names embed the timestamp)
    for (const auto& path : backups) {
        const std::string name = fs::path(path).filename().string();
        if (name.rfind("backup_", 0) != 0) continue;
        const auto tail = name.substr(7);              // <stamp>_<fname>
        const auto us = tail.find('_');
        const auto us2 = us == std::string::npos ? std::string::npos : tail.find('_', us + 1);
        if (us2 == std::string::npos) continue;
        const std::string stamp = tail.substr(0, us2); // YYYYMMDD_HHMMSS
        if (stamps.empty() || stamps.back() != stamp) stamps.push_back(stamp);
    }
    while (static_cast<int>(stamps.size()) > maxBackups_) {
        const std::string prefix = "backup_" + stamps.front() + "_";
        for (const auto& path : backups) {
            if (fs::path(path).filename().string().rfind(prefix, 0) == 0) {
                try { fs::remove(path); } catch (...) {}
            }
        }
        stamps.erase(stamps.begin());
    }
}

} // namespace BeatMate::Services::Config
