#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>
#include <mutex>
#include <cstdint>

#include <spdlog/spdlog.h>

#include "../../models/Track.h"

namespace BeatMate::Services::Library {

class TrackDatabase;

struct MissingFileInfo {
    int64_t trackId = 0;
    std::string originalPath;
    std::string title;
    std::string artist;
    std::string fileFormat;
    int64_t fileSize = 0;
    int64_t lastSeen = 0;

    std::vector<std::string> candidates;
    std::string bestMatch;
    float bestMatchScore = 0.0f;
};

struct RelocationResult {
    int64_t trackId = 0;
    std::string oldPath;
    std::string newPath;
    bool success = false;
    std::string error;
};

struct MissingScanResult {
    int totalTracks = 0;
    int missingFiles = 0;
    int relocatedFiles = 0;
    int unresolvedFiles = 0;
    std::vector<MissingFileInfo> missingInfos;
    double scanDurationMs = 0.0;
};

using MissingScanProgressCallback = std::function<void(int processed, int total, const std::string& currentFile)>;

class MissingFileManager {
public:
    explicit MissingFileManager(std::shared_ptr<TrackDatabase> database);
    ~MissingFileManager() = default;

    MissingScanResult scanForMissing(MissingScanProgressCallback progressCb = nullptr);
    MissingScanResult quickScan(const std::vector<int64_t>& trackIds);

    std::vector<MissingFileInfo> getMissingFiles() const;
    int getMissingCount() const;
    bool isFileMissing(int64_t trackId) const;

    RelocationResult relocateFile(int64_t trackId, const std::string& newPath);
    std::vector<RelocationResult> relocateFiles(const std::map<int64_t, std::string>& relocations);
    RelocationResult autoRelocate(int64_t trackId);
    std::vector<RelocationResult> autoRelocateAll();

    std::vector<std::string> findCandidates(const MissingFileInfo& info, const std::vector<std::string>& searchPaths = {});
    void addSearchPath(const std::string& path);
    void removeSearchPath(const std::string& path);
    std::vector<std::string> getSearchPaths() const;

    int removeAllMissing();
    int removeMissing(int64_t trackId);

    bool verifyFile(int64_t trackId) const;
    bool verifyFile(const std::string& filePath) const;

private:
    float calculateMatchScore(const MissingFileInfo& info, const std::string& candidatePath) const;
    std::string extractFilename(const std::string& path) const;
    std::string normalizeFilename(const std::string& filename) const;

    std::shared_ptr<TrackDatabase> database_;
    std::vector<MissingFileInfo> missingFiles_;
    std::vector<std::string> searchPaths_;
    mutable std::mutex mutex_;
};

}
