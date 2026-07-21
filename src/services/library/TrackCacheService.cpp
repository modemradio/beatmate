#include "TrackCacheService.h"
#include "TrackDatabase.h"

#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace BeatMate::Services::Library {

TrackCacheService::TrackCacheService() = default;

TrackCacheService::TrackCacheService(std::shared_ptr<TrackDatabase> database)
    : database_(std::move(database)) {
}

TrackCacheService::~TrackCacheService() {
    if (initialized_) {
        shutdown();
    }
}

bool TrackCacheService::initialize(const TrackCacheConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;

    if (config_.cachePath.empty()) {
        config_.cachePath = getDefaultCachePath();
    }

    try {
        fs::create_directories(config_.cachePath);
    } catch (const std::exception& e) {
        spdlog::error("TrackCacheService: Failed to create cache dir '{}': {}", config_.cachePath, e.what());
        return false;
    }

    initialized_ = true;

    if (config_.useDiskCache) {
        mutex_.unlock();
        loadFromDisk();
        mutex_.lock();
    }

    spdlog::info("TrackCacheService: Initialized at '{}', {} entries in memory", config_.cachePath, memoryCache_.size());
    return true;
}

void TrackCacheService::shutdown() {
    if (config_.useDiskCache) {
        saveToDisk();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    memoryCache_.clear();
    pathIndex_.clear();
    initialized_ = false;
    spdlog::info("TrackCacheService: Shutdown complete");
}

std::optional<TrackAnalysisCache> TrackCacheService::getCache(int64_t trackId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = memoryCache_.find(trackId);
    if (it != memoryCache_.end()) {
        return it->second;
    }

    if (config_.useDiskCache) {
        auto diskCache = readAnalysisFromDisk(trackId);
        if (diskCache.has_value()) {
            memoryCache_[trackId] = diskCache.value();
            if (!diskCache->filePath.empty()) {
                pathIndex_[diskCache->filePath] = trackId;
            }
            return diskCache;
        }
    }

    return std::nullopt;
}

std::optional<TrackAnalysisCache> TrackCacheService::getCacheByPath(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto pathIt = pathIndex_.find(filePath);
    if (pathIt != pathIndex_.end()) {
        auto cacheIt = memoryCache_.find(pathIt->second);
        if (cacheIt != memoryCache_.end()) {
            return cacheIt->second;
        }
    }

    return std::nullopt;
}

bool TrackCacheService::putCache(const TrackAnalysisCache& cache) {
    std::lock_guard<std::mutex> lock(mutex_);

    memoryCache_[cache.trackId] = cache;
    if (!cache.filePath.empty()) {
        pathIndex_[cache.filePath] = cache.trackId;
    }

    if (config_.useDiskCache) {
        writeAnalysisToDisk(cache);
    }

    spdlog::debug("TrackCacheService: Cached analysis for track id={}", cache.trackId);
    return true;
}

bool TrackCacheService::removeCache(int64_t trackId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = memoryCache_.find(trackId);
    if (it != memoryCache_.end()) {
        pathIndex_.erase(it->second.filePath);
        memoryCache_.erase(it);
    }

    if (config_.useDiskCache) {
        auto path = getCacheFilePath(trackId);
        try { fs::remove(path); } catch (...) {}
    }

    return true;
}

bool TrackCacheService::hasCache(int64_t trackId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (memoryCache_.count(trackId) > 0) return true;

    if (config_.useDiskCache) {
        return fs::exists(getCacheFilePath(trackId));
    }

    return false;
}

bool TrackCacheService::isCacheValid(int64_t trackId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = memoryCache_.find(trackId);
    if (it == memoryCache_.end()) return false;

    return it->second.isValid && it->second.analysisVersion == config_.analysisVersion;
}

std::vector<int64_t> TrackCacheService::getUncachedTrackIds() {
    std::vector<int64_t> uncached;
    if (!database_) return uncached;

    auto allTracks = database_->getAllTracks();

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& track : allTracks) {
        if (memoryCache_.find(track.id) == memoryCache_.end()) {
            uncached.push_back(track.id);
        }
    }

    return uncached;
}

int TrackCacheService::clearExpiredCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    int removed = 0;

    std::vector<int64_t> toRemove;
    for (const auto& [id, cache] : memoryCache_) {
        if (cache.analysisVersion != config_.analysisVersion || !cache.isValid) {
            toRemove.push_back(id);
        }
    }

    for (auto id : toRemove) {
        auto it = memoryCache_.find(id);
        if (it != memoryCache_.end()) {
            pathIndex_.erase(it->second.filePath);
            memoryCache_.erase(it);
        }
        if (config_.useDiskCache) {
            try { fs::remove(getCacheFilePath(id)); } catch (...) {}
        }
        removed++;
    }

    spdlog::info("TrackCacheService: Cleared {} expired cache entries", removed);
    return removed;
}

int TrackCacheService::clearAllCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    int count = static_cast<int>(memoryCache_.size());
    memoryCache_.clear();
    pathIndex_.clear();

    if (config_.useDiskCache) {
        try {
            for (const auto& entry : fs::directory_iterator(config_.cachePath)) {
                if (entry.path().extension() == ".bmcache") {
                    fs::remove(entry.path());
                }
            }
        } catch (...) {}
    }

    spdlog::info("TrackCacheService: Cleared all {} cache entries", count);
    return count;
}

bool TrackCacheService::saveToDisk() {
    std::lock_guard<std::mutex> lock(mutex_);

    json index;
    index["version"] = config_.analysisVersion;
    index["count"] = memoryCache_.size();

    json entries = json::array();
    for (const auto& [id, cache] : memoryCache_) {
        json entry;
        entry["trackId"] = cache.trackId;
        entry["filePath"] = cache.filePath;
        entry["bpm"] = cache.bpm;
        entry["key"] = cache.key;
        entry["energy"] = cache.energy;
        entry["analyzedAt"] = cache.analyzedAt;
        entry["isValid"] = cache.isValid;
        entries.push_back(entry);
    }
    index["entries"] = entries;

    std::string indexPath = config_.cachePath + "/cache_index.json";
    try {
        std::ofstream file(indexPath);
        file << index.dump(2);
        spdlog::debug("TrackCacheService: Saved index with {} entries", memoryCache_.size());
        return true;
    } catch (const std::exception& e) {
        spdlog::error("TrackCacheService: Failed to save index: {}", e.what());
        return false;
    }
}

bool TrackCacheService::loadFromDisk() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string indexPath = config_.cachePath + "/cache_index.json";
    if (!fs::exists(indexPath)) return false;

    try {
        std::ifstream file(indexPath);
        json index = json::parse(file);

        auto entries = index.value("entries", json::array());
        for (const auto& entry : entries) {
            TrackAnalysisCache cache;
            cache.trackId = entry.value("trackId", int64_t(0));
            cache.filePath = entry.value("filePath", "");
            cache.bpm = entry.value("bpm", 0.0);
            cache.key = entry.value("key", "");
            cache.energy = entry.value("energy", 0.0f);
            cache.analyzedAt = entry.value("analyzedAt", int64_t(0));
            cache.isValid = entry.value("isValid", false);
            cache.analysisVersion = index.value("version", "");

            memoryCache_[cache.trackId] = cache;
            if (!cache.filePath.empty()) {
                pathIndex_[cache.filePath] = cache.trackId;
            }
        }

        spdlog::info("TrackCacheService: Loaded {} cache entries from disk", memoryCache_.size());
        return true;
    } catch (const std::exception& e) {
        spdlog::error("TrackCacheService: Failed to load index: {}", e.what());
        return false;
    }
}

int64_t TrackCacheService::getCacheCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int64_t>(memoryCache_.size());
}

int64_t TrackCacheService::getCacheSizeBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t size = 0;

    for (const auto& [id, cache] : memoryCache_) {
        size += sizeof(TrackAnalysisCache);
        size += static_cast<int64_t>(cache.waveformPeaks.size() * sizeof(float));
        size += static_cast<int64_t>(cache.waveformRMS.size() * sizeof(float));
        size += static_cast<int64_t>(cache.beatPositions.size() * sizeof(double));
        size += static_cast<int64_t>(cache.energyProfile.size() * sizeof(float));
        size += static_cast<int64_t>(cache.bpmHistory.size() * sizeof(double));
    }

    return size;
}

std::string TrackCacheService::getCacheFilePath(int64_t trackId) const {
    return config_.cachePath + "/track_" + std::to_string(trackId) + ".bmcache";
}

bool TrackCacheService::writeAnalysisToDisk(const TrackAnalysisCache& cache) {
    std::string path = getCacheFilePath(cache.trackId);
    try {
        json j;
        j["trackId"] = cache.trackId;
        j["filePath"] = cache.filePath;
        j["bpm"] = cache.bpm;
        j["bpmConfidence"] = cache.bpmConfidence;
        j["key"] = cache.key;
        j["camelotKey"] = cache.camelotKey;
        j["openKey"] = cache.openKey;
        j["keyConfidence"] = cache.keyConfidence;
        j["energy"] = cache.energy;
        j["peakLoudness"] = cache.peakLoudness;
        j["averageLoudness"] = cache.averageLoudness;
        j["firstBeatOffset"] = cache.firstBeatOffset;
        j["waveformResolution"] = cache.waveformResolution;
        j["analyzedAt"] = cache.analyzedAt;
        j["fileModifiedAt"] = cache.fileModifiedAt;
        j["analysisVersion"] = cache.analysisVersion;
        j["isValid"] = cache.isValid;

        std::ofstream file(path, std::ios::binary);
        file << j.dump();
        return true;
    } catch (const std::exception& e) {
        spdlog::debug("TrackCacheService: Failed to write cache file: {}", e.what());
        return false;
    }
}

std::optional<TrackAnalysisCache> TrackCacheService::readAnalysisFromDisk(int64_t trackId) {
    std::string path = getCacheFilePath(trackId);
    if (!fs::exists(path)) return std::nullopt;

    try {
        std::ifstream file(path, std::ios::binary);
        json j = json::parse(file);

        TrackAnalysisCache cache;
        cache.trackId = j.value("trackId", int64_t(0));
        cache.filePath = j.value("filePath", "");
        cache.bpm = j.value("bpm", 0.0);
        cache.bpmConfidence = j.value("bpmConfidence", 0.0);
        cache.key = j.value("key", "");
        cache.camelotKey = j.value("camelotKey", "");
        cache.openKey = j.value("openKey", "");
        cache.keyConfidence = j.value("keyConfidence", 0.0);
        cache.energy = j.value("energy", 0.0f);
        cache.peakLoudness = j.value("peakLoudness", 0.0f);
        cache.averageLoudness = j.value("averageLoudness", 0.0f);
        cache.firstBeatOffset = j.value("firstBeatOffset", 0.0);
        cache.waveformResolution = j.value("waveformResolution", 0);
        cache.analyzedAt = j.value("analyzedAt", int64_t(0));
        cache.fileModifiedAt = j.value("fileModifiedAt", int64_t(0));
        cache.analysisVersion = j.value("analysisVersion", "");
        cache.isValid = j.value("isValid", false);

        return cache;
    } catch (const std::exception& e) {
        spdlog::debug("TrackCacheService: Failed to read cache file for track {}: {}", trackId, e.what());
        return std::nullopt;
    }
}

std::string TrackCacheService::getDefaultCachePath() const {
#ifdef _WIN32
    std::string appData = std::getenv("LOCALAPPDATA") ? std::getenv("LOCALAPPDATA") : "";
    if (!appData.empty()) return appData + "/BeatMate/cache/analysis";
#else
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";
    if (!home.empty()) return home + "/.cache/beatmate/analysis";
#endif
    return "./beatmate_cache";
}

} // namespace BeatMate::Services::Library
