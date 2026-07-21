#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <functional>
#include <optional>
#include <chrono>
#include <cstdint>

#include <spdlog/spdlog.h>

#include "../../models/Track.h"

namespace BeatMate::Services::Library {

class TrackDatabase;

struct CacheEntry {
    Models::Track track;
    std::chrono::steady_clock::time_point lastAccess;
    std::chrono::steady_clock::time_point insertTime;
    int accessCount = 0;
    bool dirty = false;
};

struct CacheStats {
    int64_t totalEntries = 0;
    int64_t hits = 0;
    int64_t misses = 0;
    int64_t evictions = 0;
    int64_t memoryUsageBytes = 0;
    double hitRatio() const { return (hits + misses) > 0 ? static_cast<double>(hits) / (hits + misses) : 0.0; }
};

struct CacheConfig {
    int maxEntries = 10000;
    int ttlSeconds = 3600;
    int cleanupIntervalSeconds = 300;
    bool preloadOnStart = true;
    int preloadLimit = 1000;
    bool persistToDisk = true;
};

class CollectionCacheManager {
public:
    explicit CollectionCacheManager(std::shared_ptr<TrackDatabase> database);
    ~CollectionCacheManager() = default;

    bool initialize(const CacheConfig& config = {});
    void shutdown();

    std::optional<Models::Track> getTrack(int64_t trackId);
    std::optional<Models::Track> getTrackByPath(const std::string& filePath);
    void putTrack(const Models::Track& track);
    void invalidateTrack(int64_t trackId);
    void invalidateAll();

    std::vector<Models::Track> getTracks(const std::vector<int64_t>& trackIds);
    void putTracks(const std::vector<Models::Track>& tracks);

    void preloadRecentTracks(int limit = 1000);
    void preloadAllTracks();
    void preloadTracksByQuery(const std::string& sql);

    void cleanup();
    void compact();
    int evictLRU(int count);
    int evictExpired();

    CacheStats getStats() const;
    void resetStats();
    int64_t getCacheSize() const;

    void rebuildPathIndex();

private:
    void evictIfNeeded();
    int64_t estimateMemoryUsage() const;

    std::shared_ptr<TrackDatabase> database_;
    CacheConfig config_;
    mutable std::mutex mutex_;

    std::map<int64_t, CacheEntry> cache_;
    std::map<std::string, int64_t> pathIndex_;

    mutable CacheStats stats_;
    bool initialized_ = false;
};

}
