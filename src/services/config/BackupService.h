#pragma once
#include <juce_events/juce_events.h>
#include <string>
#include <vector>
#include <memory>
#include <juce_core/juce_core.h>

namespace BeatMate::Services::Config {

class BackupService : private juce::Timer {
public:
    BackupService();
    ~BackupService() override;

    bool createBackup(const std::string& dbPath = "");
    bool restoreBackup(const std::string& backupPath);
    std::vector<std::string> listBackups() const;
    void setMaxBackups(int max) { maxBackups_ = max; }
    void startAutoBackup(int intervalMinutes = 60);
    void stopAutoBackup();
    void setBackupDir(const std::string& dir) { backupDir_ = dir; }

private:
    void timerCallback() override;
    void pruneOldBackups();

    std::string backupDir_;
    std::string dbPath_;
    int maxBackups_ = 10;
};

} // namespace BeatMate::Services::Config
