#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <optional>
#include <cstdint>

#include <spdlog/spdlog.h>

namespace BeatMate::Services::Library {

class TrackDatabase;

struct TrackAnalysisCache {
    int64_t trackId = 0;
    std::string filePath;

    double bpm = 0.0;
    double bpmConfidence = 0.0;
    std::vector<double> bpmHistory;       // BPM over time (for variable BPM tracks)

    std::string key;
    std::string camelotKey;
    std::string openKey;
    double keyConfidence = 0.0;

    float energy = 0.0f;
    float peakLoudness = 0.0f;
    float averageLoudness = 0.0f;
    std::vector<float> energyProfile;     // Energy over time

    double firstBeatOffset = 0.0;
    std::vector<double> beatPositions;

    std::vector<float> waveformPeaks;
    std::vector<float> waveformRMS;
    int waveformResolution = 0;           // samples per pixel

    std::vector<float> spectralCentroid;
    std::vector<float> spectralFlux;

    int64_t analyzedAt = 0;
    int64_t fileModifiedAt = 0;
    std::string analysisVersion;
    bool isValid = false;
};

struct TrackCacheConfig {
    std::string cachePath;               // Directory for cache files
    int maxCacheEntries = 50000;
    bool useDiskCache = true;
    bool compressCache = false;
    std::string analysisVersion = "1.0";
};

class TrackCacheService {
public:
    TrackCacheService();
    explicit TrackCacheService(std::shared_ptr<TrackDatabase> database);
    ~TrackCacheService();

    bool initialize(const TrackCacheConfig& config = {});
    void shutdown();

    std::optional<TrackAnalysisCache> getCache(int64_t trackId);
    std::optional<TrackAnalysisCache> getCacheByPath(const std::string& filePath);
    bool putCache(const TrackAnalysisCache& cache);
    bool removeCache(int64_t trackId);
    bool hasCache(int64_t trackId) const;
    bool isCacheValid(int64_t trackId) const;

    std::vector<int64_t> getUncachedTrackIds();
    int clearExpiredCache();
    int clearAllCache();

    bool saveToDisk();
    bool loadFromDisk();

    int64_t getCacheCount() const;
    int64_t getCacheSizeBytes() const;
    std::string getCachePath() const { return config_.cachePath; }

private:
    std::string getCacheFilePath(int64_t trackId) const;
    bool writeAnalysisToDisk(const TrackAnalysisCache& cache);
    std::optional<TrackAnalysisCache> readAnalysisFromDisk(int64_t trackId);
    std::string getDefaultCachePath() const;

    std::shared_ptr<TrackDatabase> database_;
    TrackCacheConfig config_;
    mutable std::mutex mutex_;

    std::map<int64_t, TrackAnalysisCache> memoryCache_;
    std::map<std::string, int64_t> pathIndex_;

    bool initialized_ = false;
};

} // namespace BeatMate::Services::Library
