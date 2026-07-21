#include "SetBackupService.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <functional>

namespace fs = std::filesystem;

namespace BeatMate::Services::Preparation {

bool SetBackupService::saveSnapshot(const std::string& filePath, const SetSnapshot& snapshot) {
    try {
        nlohmann::json j;
        j["id"] = snapshot.id;
        j["name"] = snapshot.name;
        j["timestamp"] = snapshot.timestamp;
        j["notes"] = snapshot.notes;
        j["trackCount"] = snapshot.tracks.size();

        nlohmann::json tracksJson = nlohmann::json::array();
        for (const auto& t : snapshot.tracks) {
            nlohmann::json tj;
            to_json(tj, t);
            tracksJson.push_back(tj);
        }
        j["tracks"] = tracksJson;

        std::string content = j.dump(2);
        j["checksum"] = computeChecksum(content);

        std::ofstream file(filePath);
        if (!file.is_open()) {
            spdlog::error("SetBackupService: Cannot open file for writing: {}", filePath);
            return false;
        }
        file << j.dump(2);
        file.close();
        spdlog::info("SetBackupService: Snapshot saved to {}", filePath);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("SetBackupService: Save failed: {}", e.what());
        return false;
    }
}

SetSnapshot SetBackupService::loadSnapshot(const std::string& filePath) {
    SetSnapshot snapshot;
    try {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            spdlog::error("SetBackupService: Cannot open file: {}", filePath);
            return snapshot;
        }

        nlohmann::json j;
        file >> j;
        file.close();

        snapshot.id = j.value("id", int64_t(0));
        snapshot.name = j.value("name", "");
        snapshot.timestamp = j.value("timestamp", "");
        snapshot.notes = j.value("notes", "");
        snapshot.checksum = j.value("checksum", "");

        if (j.contains("tracks") && j["tracks"].is_array()) {
            for (const auto& tj : j["tracks"]) {
                Models::Track t;
                from_json(tj, t);
                snapshot.tracks.push_back(t);
            }
        }

        spdlog::info("SetBackupService: Loaded snapshot '{}' with {} tracks", snapshot.name, snapshot.tracks.size());
    } catch (const std::exception& e) {
        spdlog::error("SetBackupService: Load failed: {}", e.what());
    }
    return snapshot;
}

bool SetBackupService::exportToJson(const std::string& filePath, const std::vector<Models::Track>& tracks, const std::string& name) {
    SetSnapshot snapshot;
    snapshot.name = name;
    snapshot.timestamp = generateTimestamp();
    snapshot.tracks = tracks;
    return saveSnapshot(filePath, snapshot);
}

std::vector<Models::Track> SetBackupService::importFromJson(const std::string& filePath) {
    auto snapshot = loadSnapshot(filePath);
    return snapshot.tracks;
}

std::vector<BackupInfo> SetBackupService::listBackups(const std::string& directory) {
    std::vector<BackupInfo> backups;
    try {
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.path().extension() == ".json" || entry.path().extension() == ".beatmate") {
                BackupInfo info;
                info.filePath = entry.path().string();
                info.name = entry.path().stem().string();
                info.fileSize = static_cast<int64_t>(entry.file_size());

                auto ftime = entry.last_write_time();
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                auto time = std::chrono::system_clock::to_time_t(sctp);
                std::ostringstream ss;
                ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
                info.createdAt = ss.str();

                try {
                    std::ifstream f(entry.path());
                    nlohmann::json j;
                    f >> j;
                    info.trackCount = j.value("trackCount", 0);
                } catch (...) {}

                backups.push_back(info);
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("SetBackupService: List backups failed: {}", e.what());
    }
    spdlog::info("SetBackupService: Found {} backups in {}", backups.size(), directory);
    return backups;
}

bool SetBackupService::deleteBackup(const std::string& filePath) {
    try {
        if (fs::remove(filePath)) {
            spdlog::info("SetBackupService: Deleted backup {}", filePath);
            return true;
        }
    } catch (const std::exception& e) {
        spdlog::error("SetBackupService: Delete failed: {}", e.what());
    }
    return false;
}

std::string SetBackupService::autoBackup(const std::string& directory, const std::vector<Models::Track>& tracks, const std::string& setName) {
    fs::create_directories(directory);
    std::string timestamp = generateTimestamp();
    std::string safeName = setName;
    std::replace(safeName.begin(), safeName.end(), ' ', '_');
    std::string fileName = safeName + "_" + timestamp + ".json";
    std::string filePath = (fs::path(directory) / fileName).string();
    exportToJson(filePath, tracks, setName);
    return filePath;
}

std::string SetBackupService::generateTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S");
    return ss.str();
}

std::string SetBackupService::computeChecksum(const std::string& data) const {
    std::hash<std::string> hasher;
    auto hash = hasher(data);
    std::ostringstream ss;
    ss << std::hex << hash;
    return ss.str();
}

} // namespace BeatMate::Services::Preparation
