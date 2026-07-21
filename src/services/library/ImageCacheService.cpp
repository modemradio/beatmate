#include "ImageCacheService.h"
#include "TrackDatabase.h"
#include "TrackMetadata.h"

#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <chrono>

namespace fs = std::filesystem;

namespace BeatMate::Services::Library {

ImageCacheService::ImageCacheService() = default;

ImageCacheService::ImageCacheService(std::shared_ptr<TrackDatabase> database,
                                       std::shared_ptr<TrackMetadata> metadata)
    : database_(std::move(database))
    , metadata_(std::move(metadata)) {
}

bool ImageCacheService::initialize(const ImageCacheConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;

    if (config_.cacheDirectory.empty()) {
        config_.cacheDirectory = getDefaultCacheDir();
    }

    try {
        fs::create_directories(config_.cacheDirectory);
        fs::create_directories(config_.cacheDirectory + "/small");
        fs::create_directories(config_.cacheDirectory + "/medium");
        fs::create_directories(config_.cacheDirectory + "/large");
        fs::create_directories(config_.cacheDirectory + "/xlarge");
    } catch (const std::exception& e) {
        spdlog::error("ImageCacheService: Failed to create cache dirs: {}", e.what());
        return false;
    }

    initialized_ = true;
    spdlog::info("ImageCacheService: Initialized at '{}'", config_.cacheDirectory);
    return true;
}

std::optional<CachedImage> ImageCacheService::getAlbumArt(int64_t trackId, ThumbnailSize size) {
    std::string key = getCacheKey(trackId, size);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = memoryCache_.find(key);
        if (it != memoryCache_.end()) {
            it->second.accessCount++;
            it->second.lastAccessed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            stats_.hits++;
            return it->second;
        }
        stats_.misses++;
    }

    std::string cachePath = getCacheFilePath(trackId, size);
    if (fs::exists(cachePath)) {
        try {
            std::ifstream file(cachePath, std::ios::binary);
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                                       std::istreambuf_iterator<char>());

            if (!data.empty()) {
                CachedImage img;
                img.trackId = trackId;
                img.imageData = std::move(data);
                img.width = static_cast<int>(size);
                img.height = static_cast<int>(size);
                img.format = config_.thumbnailFormat;
                img.lastAccessed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                img.accessCount = 1;

                std::lock_guard<std::mutex> lock(mutex_);
                if (static_cast<int>(memoryCache_.size()) >= config_.maxMemoryCacheEntries) {
                    auto oldest = memoryCache_.begin();
                    for (auto it = memoryCache_.begin(); it != memoryCache_.end(); ++it) {
                        if (it->second.lastAccessed < oldest->second.lastAccessed) {
                            oldest = it;
                        }
                    }
                    memoryCache_.erase(oldest);
                }
                memoryCache_[key] = img;
                return img;
            }
        } catch (...) {}
    }

    auto rawArt = getRawAlbumArt(trackId);
    if (rawArt.empty()) return std::nullopt;

    auto thumbnail = resizeImage(rawArt, static_cast<int>(size));
    if (thumbnail.empty()) thumbnail = rawArt; // Use original if resize fails

    try {
        std::ofstream file(cachePath, std::ios::binary);
        file.write(reinterpret_cast<const char*>(thumbnail.data()), static_cast<std::streamsize>(thumbnail.size()));
    } catch (...) {}

    CachedImage img;
    img.trackId = trackId;
    img.imageData = thumbnail;
    img.width = static_cast<int>(size);
    img.height = static_cast<int>(size);
    img.format = config_.thumbnailFormat;
    img.lastAccessed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    img.accessCount = 1;

    std::lock_guard<std::mutex> lock(mutex_);
    memoryCache_[key] = img;
    return img;
}

std::optional<CachedImage> ImageCacheService::getAlbumArtByPath(const std::string& filePath, ThumbnailSize size) {
    if (database_) {
        auto trackOpt = database_->getTrackByPath(filePath);
        if (trackOpt.has_value()) {
            return getAlbumArt(trackOpt->id, size);
        }
    }

    if (!metadata_) return std::nullopt;

    auto rawArt = metadata_->readAlbumArt(filePath);
    if (rawArt.empty()) return std::nullopt;

    CachedImage img;
    img.trackId = 0;
    img.imageData = resizeImage(rawArt, static_cast<int>(size));
    if (img.imageData.empty()) img.imageData = rawArt;
    img.width = static_cast<int>(size);
    img.height = static_cast<int>(size);
    img.format = config_.thumbnailFormat;
    return img;
}

std::vector<uint8_t> ImageCacheService::getRawAlbumArt(int64_t trackId) {
    if (!metadata_ || !database_) return {};

    auto trackOpt = database_->getTrack(trackId);
    if (!trackOpt.has_value()) return {};

    if (!trackOpt->albumArt.empty()) {
        return trackOpt->albumArt;
    }

    return metadata_->readAlbumArt(trackOpt->filePath);
}

int ImageCacheService::precacheAlbumArts(const std::vector<int64_t>& trackIds, ThumbnailSize size,
                                           std::function<void(int, int)> progressCb) {
    int cached = 0;
    int total = static_cast<int>(trackIds.size());

    for (size_t i = 0; i < trackIds.size(); ++i) {
        auto art = getAlbumArt(trackIds[i], size);
        if (art.has_value()) cached++;

        if (progressCb && (i + 1) % 10 == 0) {
            progressCb(static_cast<int>(i + 1), total);
        }
    }

    spdlog::info("ImageCacheService: Pre-cached {} of {} album arts", cached, total);
    return cached;
}

bool ImageCacheService::invalidate(int64_t trackId) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> keysToRemove;
    for (const auto& [key, _] : memoryCache_) {
        if (key.find(std::to_string(trackId) + "_") == 0) {
            keysToRemove.push_back(key);
        }
    }
    for (const auto& key : keysToRemove) {
        memoryCache_.erase(key);
    }

    for (auto size : {ThumbnailSize::Small, ThumbnailSize::Medium, ThumbnailSize::Large, ThumbnailSize::XLarge}) {
        auto path = getCacheFilePath(trackId, size);
        try { fs::remove(path); } catch (...) {}
    }

    return true;
}

int ImageCacheService::invalidateAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    int count = static_cast<int>(memoryCache_.size());
    memoryCache_.clear();

    try {
        for (const auto& subdir : {"small", "medium", "large", "xlarge"}) {
            std::string path = config_.cacheDirectory + "/" + subdir;
            if (fs::exists(path)) {
                for (const auto& entry : fs::directory_iterator(path)) {
                    fs::remove(entry.path());
                }
            }
        }
    } catch (...) {}

    spdlog::info("ImageCacheService: Invalidated all cache ({} entries)", count);
    return count;
}

int ImageCacheService::cleanupOld(int maxAgeDays) {
    int removed = 0;
    auto now = std::chrono::system_clock::now();

    try {
        for (const auto& subdir : {"small", "medium", "large", "xlarge"}) {
            std::string path = config_.cacheDirectory + "/" + subdir;
            if (!fs::exists(path)) continue;

            for (const auto& entry : fs::directory_iterator(path)) {
                auto ftime = fs::last_write_time(entry.path());
                auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
                auto age = std::chrono::duration_cast<std::chrono::hours>(now - sctp).count() / 24;

                if (age > maxAgeDays) {
                    fs::remove(entry.path());
                    removed++;
                }
            }
        }
    } catch (...) {}

    spdlog::debug("ImageCacheService: Cleaned up {} old cache files", removed);
    return removed;
}

bool ImageCacheService::setAlbumArt(int64_t trackId, const std::vector<uint8_t>& imageData) {
    if (!metadata_ || !database_) return false;

    auto trackOpt = database_->getTrack(trackId);
    if (!trackOpt.has_value()) return false;

    bool success = metadata_->writeAlbumArt(trackOpt->filePath, imageData);
    if (success) {
        invalidate(trackId);
    }
    return success;
}

ImageCacheStats ImageCacheService::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto s = stats_;
    s.totalImages = static_cast<int64_t>(memoryCache_.size());
    s.memoryCacheSize = 0;
    for (const auto& [_, img] : memoryCache_) {
        s.memoryCacheSize += static_cast<int64_t>(img.imageData.size());
    }
    return s;
}

int64_t ImageCacheService::getMemoryCacheCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int64_t>(memoryCache_.size());
}

int64_t ImageCacheService::getDiskCacheCount() const {
    int64_t count = 0;
    try {
        for (const auto& subdir : {"small", "medium", "large", "xlarge"}) {
            std::string path = config_.cacheDirectory + "/" + subdir;
            if (fs::exists(path)) {
                for (const auto& _ : fs::directory_iterator(path)) {
                    (void)_;
                    count++;
                }
            }
        }
    } catch (...) {}
    return count;
}

std::vector<uint8_t> ImageCacheService::getDefaultAlbumArt(ThumbnailSize size) const {
    static const std::vector<uint8_t> defaultPng = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, // PNG signature
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, // IHDR chunk
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, // 1x1
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, // RGBA
        0x89, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, // IDAT chunk
        0x54, 0x78, 0x9C, 0x62, 0x00, 0x00, 0x00, 0x02, // compressed data
        0x00, 0x01, 0xE5, 0x27, 0xDE, 0xFC, 0x00, 0x00, // ...
        0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, // IEND
        0x60, 0x82
    };
    (void)size;
    return defaultPng;
}

std::string ImageCacheService::getCacheFilePath(int64_t trackId, ThumbnailSize size) const {
    std::string subdir;
    switch (size) {
        case ThumbnailSize::Small:  subdir = "small"; break;
        case ThumbnailSize::Medium: subdir = "medium"; break;
        case ThumbnailSize::Large:  subdir = "large"; break;
        case ThumbnailSize::XLarge: subdir = "xlarge"; break;
    }
    return config_.cacheDirectory + "/" + subdir + "/" + std::to_string(trackId) + "." + config_.thumbnailFormat;
}

std::string ImageCacheService::getDefaultCacheDir() const {
#ifdef _WIN32
    std::string appData = std::getenv("LOCALAPPDATA") ? std::getenv("LOCALAPPDATA") : "";
    if (!appData.empty()) return appData + "/BeatMate/cache/images";
#else
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";
    if (!home.empty()) return home + "/.cache/beatmate/images";
#endif
    return "./beatmate_images";
}

std::string ImageCacheService::getCacheKey(int64_t trackId, ThumbnailSize size) const {
    return std::to_string(trackId) + "_" + std::to_string(static_cast<int>(size));
}

std::vector<uint8_t> ImageCacheService::resizeImage(const std::vector<uint8_t>& original, int targetSize) const {
    (void)targetSize;
    return original;
}

} // namespace BeatMate::Services::Library
