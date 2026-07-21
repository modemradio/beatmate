#include "CollectionCacheManager.h"
#include "TrackDatabase.h"

#include <spdlog/spdlog.h>
#include <algorithm>

namespace BeatMate::Services::Library {

CollectionCacheManager::CollectionCacheManager(std::shared_ptr<TrackDatabase> database)
    : database_(std::move(database)) {
}

bool CollectionCacheManager::initialize(const CacheConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    cache_.clear();
    pathIndex_.clear();
    stats_ = {};
    initialized_ = true;

    spdlog::info("CollectionCacheManager: Initialized (max={}, ttl={}s)", config_.maxEntries, config_.ttlSeconds);

    if (config_.preloadOnStart && database_) {
        // Unlock mutex for preload (it acquires its own lock)
        mutex_.unlock();
        preloadRecentTracks(config_.preloadLimit);
        mutex_.lock();
    }

    return true;
}

void CollectionCacheManager::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
    pathIndex_.clear();
    initialized_ = false;
    spdlog::info("CollectionCacheManager: Shutdown, stats: hits={}, misses={}, ratio={:.2f}",
                 stats_.hits, stats_.misses, stats_.hitRatio());
}

std::optional<Models::Track> CollectionCacheManager::getTrack(int64_t trackId) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(trackId);
        if (it != cache_.end()) {
            auto now = std::chrono::steady_clock::now();
            auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.insertTime).count();
            if (age < config_.ttlSeconds) {
                it->second.lastAccess = now;
                it->second.accessCount++;
                stats_.hits++;
                return it->second.track;
            } else {
                pathIndex_.erase(it->second.track.filePath);
                cache_.erase(it);
                stats_.evictions++;
            }
        }
        stats_.misses++;
    }

    if (database_) {
        auto track = database_->getTrack(trackId);
        if (track.has_value()) {
            putTrack(track.value());
            return track;
        }
    }

    return std::nullopt;
}

std::optional<Models::Track> CollectionCacheManager::getTrackByPath(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto pathIt = pathIndex_.find(filePath);
    if (pathIt != pathIndex_.end()) {
        auto cacheIt = cache_.find(pathIt->second);
        if (cacheIt != cache_.end()) {
            auto now = std::chrono::steady_clock::now();
            auto age = std::chrono::duration_cast<std::chrono::seconds>(now - cacheIt->second.insertTime).count();
            if (age < config_.ttlSeconds) {
                cacheIt->second.lastAccess = now;
                cacheIt->second.accessCount++;
                stats_.hits++;
                return cacheIt->second.track;
            }
        }
    }
    stats_.misses++;

    if (database_) {
        auto track = database_->getTrackByPath(filePath);
        if (track.has_value()) {
            // Must unlock to call putTrack
            mutex_.unlock();
            putTrack(track.value());
            mutex_.lock();
            return track;
        }
    }

    return std::nullopt;
}

void CollectionCacheManager::putTrack(const Models::Track& track) {
    std::lock_guard<std::mutex> lock(mutex_);
    evictIfNeeded();

    auto now = std::chrono::steady_clock::now();
    CacheEntry entry;
    entry.track = track;
    entry.lastAccess = now;
    entry.insertTime = now;
    entry.accessCount = 1;

    cache_[track.id] = entry;
    if (!track.filePath.empty()) {
        pathIndex_[track.filePath] = track.id;
    }
}

void CollectionCacheManager::invalidateTrack(int64_t trackId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(trackId);
    if (it != cache_.end()) {
        pathIndex_.erase(it->second.track.filePath);
        cache_.erase(it);
        spdlog::debug("CollectionCacheManager: Invalidated track id={}", trackId);
    }
}

void CollectionCacheManager::invalidateAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
    pathIndex_.clear();
    spdlog::info("CollectionCacheManager: All entries invalidated");
}

std::vector<Models::Track> CollectionCacheManager::getTracks(const std::vector<int64_t>& trackIds) {
    std::vector<Models::Track> result;
    result.reserve(trackIds.size());
    for (auto id : trackIds) {
        auto track = getTrack(id);
        if (track.has_value()) {
            result.push_back(std::move(track.value()));
        }
    }
    return result;
}

void CollectionCacheManager::putTracks(const std::vector<Models::Track>& tracks) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& track : tracks) {
        evictIfNeeded();
        auto now = std::chrono::steady_clock::now();
        CacheEntry entry;
        entry.track = track;
        entry.lastAccess = now;
        entry.insertTime = now;
        entry.accessCount = 1;
        cache_[track.id] = entry;
        if (!track.filePath.empty()) {
            pathIndex_[track.filePath] = track.id;
        }
    }
}

void CollectionCacheManager::preloadRecentTracks(int limit) {
    if (!database_) return;

    std::string sql = "SELECT * FROM tracks ORDER BY date_added DESC LIMIT " + std::to_string(limit);
    auto tracks = database_->getTracksByQuery(sql);

    putTracks(tracks);
    spdlog::info("CollectionCacheManager: Preloaded {} recent tracks", tracks.size());
}

void CollectionCacheManager::preloadAllTracks() {
    if (!database_) return;

    auto tracks = database_->getAllTracks();
    putTracks(tracks);
    spdlog::info("CollectionCacheManager: Preloaded all {} tracks", tracks.size());
}

void CollectionCacheManager::preloadTracksByQuery(const std::string& sql) {
    if (!database_) return;

    auto tracks = database_->getTracksByQuery(sql);
    putTracks(tracks);
    spdlog::debug("CollectionCacheManager: Preloaded {} tracks by query", tracks.size());
}

void CollectionCacheManager::cleanup() {
    int expired = evictExpired();
    spdlog::debug("CollectionCacheManager: Cleanup removed {} expired entries, {} remaining",
                  expired, cache_.size());
}

void CollectionCacheManager::compact() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    auto threshold = std::chrono::seconds(config_.ttlSeconds * 2);

    std::vector<int64_t> toRemove;
    for (const auto& [id, entry] : cache_) {
        if (std::chrono::duration_cast<std::chrono::seconds>(now - entry.lastAccess) > threshold) {
            toRemove.push_back(id);
        }
    }

    for (auto id : toRemove) {
        auto it = cache_.find(id);
        if (it != cache_.end()) {
            pathIndex_.erase(it->second.track.filePath);
            cache_.erase(it);
        }
    }

    spdlog::debug("CollectionCacheManager: Compacted, removed {} cold entries", toRemove.size());
}

int CollectionCacheManager::evictLRU(int count) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (cache_.empty() || count <= 0) return 0;

    std::vector<std::pair<int64_t, std::chrono::steady_clock::time_point>> entries;
    for (const auto& [id, entry] : cache_) {
        entries.emplace_back(id, entry.lastAccess);
    }
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    int evicted = 0;
    for (size_t i = 0; i < static_cast<size_t>(count) && i < entries.size(); ++i) {
        auto it = cache_.find(entries[i].first);
        if (it != cache_.end()) {
            pathIndex_.erase(it->second.track.filePath);
            cache_.erase(it);
            evicted++;
            stats_.evictions++;
        }
    }

    return evicted;
}

int CollectionCacheManager::evictExpired() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();

    std::vector<int64_t> expired;
    for (const auto& [id, entry] : cache_) {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - entry.insertTime).count();
        if (age >= config_.ttlSeconds) {
            expired.push_back(id);
        }
    }

    for (auto id : expired) {
        auto it = cache_.find(id);
        if (it != cache_.end()) {
            pathIndex_.erase(it->second.track.filePath);
            cache_.erase(it);
            stats_.evictions++;
        }
    }

    return static_cast<int>(expired.size());
}

CacheStats CollectionCacheManager::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto s = stats_;
    s.totalEntries = static_cast<int64_t>(cache_.size());
    s.memoryUsageBytes = estimateMemoryUsage();
    return s;
}

void CollectionCacheManager::resetStats() {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_ = {};
}

int64_t CollectionCacheManager::getCacheSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int64_t>(cache_.size());
}

void CollectionCacheManager::rebuildPathIndex() {
    std::lock_guard<std::mutex> lock(mutex_);
    pathIndex_.clear();
    for (const auto& [id, entry] : cache_) {
        if (!entry.track.filePath.empty()) {
            pathIndex_[entry.track.filePath] = id;
        }
    }
    spdlog::debug("CollectionCacheManager: Path index rebuilt with {} entries", pathIndex_.size());
}

void CollectionCacheManager::evictIfNeeded() {
    // Called with mutex held
    if (static_cast<int>(cache_.size()) >= config_.maxEntries) {
        auto oldest = cache_.begin();
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->second.lastAccess < oldest->second.lastAccess) {
                oldest = it;
            }
        }
        if (oldest != cache_.end()) {
            pathIndex_.erase(oldest->second.track.filePath);
            cache_.erase(oldest);
            stats_.evictions++;
        }
    }
}

int64_t CollectionCacheManager::estimateMemoryUsage() const {
    // Rough estimate: each track ~500 bytes + overhead
    return static_cast<int64_t>(cache_.size()) * 600;
}

} // namespace BeatMate::Services::Library
