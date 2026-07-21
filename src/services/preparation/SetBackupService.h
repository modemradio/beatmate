#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "../../models/Track.h"
#include <nlohmann/json.hpp>

namespace BeatMate::Services::Preparation {

struct SetSnapshot {
    int64_t id = 0;
    std::string name;
    std::string timestamp;
    std::vector<Models::Track> tracks;
    std::string notes;
    std::string checksum;
};

struct BackupInfo {
    std::string filePath;
    std::string name;
    std::string createdAt;
    int trackCount = 0;
    int64_t fileSize = 0;
};

class SetBackupService {
public:
    SetBackupService() = default;

    bool saveSnapshot(const std::string& filePath, const SetSnapshot& snapshot);
    SetSnapshot loadSnapshot(const std::string& filePath);
    bool exportToJson(const std::string& filePath, const std::vector<Models::Track>& tracks, const std::string& name);
    std::vector<Models::Track> importFromJson(const std::string& filePath);
    std::vector<BackupInfo> listBackups(const std::string& directory);
    bool deleteBackup(const std::string& filePath);
    std::string autoBackup(const std::string& directory, const std::vector<Models::Track>& tracks, const std::string& setName);

private:
    std::string generateTimestamp() const;
    std::string computeChecksum(const std::string& data) const;
};

}
