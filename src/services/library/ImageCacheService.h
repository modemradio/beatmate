#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <optional>
#include <functional>
#include <cstdint>

#include <spdlog/spdlog.h>

namespace BeatMate::Services::Library {

class TrackDatabase;
class TrackMetadata;

enum class ThumbnailSize {
    Small = 64,
    Medium = 128,
    Large = 256,
    XLarge = 512
};

struct CachedImage {
    int64_t trackId = 0;
    std::vector<uint8_t> imageData;
    int width = 0;
    int height = 0;
    std::string format;       // "png", "jpg"
    int64_t lastAccessed = 0;
    int accessCount = 0;
};

struct ImageCacheStats {
    int64_t totalImages = 0;
    int64_t memoryCacheSize = 0;
    int64_t diskCacheSize = 0;
    int64_t hits = 0;
    int64_t misses = 0;
    double hitRatio() const { return (hits + misses) > 0 ? static_cast<double>(hits) / (hits + misses) : 0.0; }
};

struct ImageCacheConfig {
    std::string cacheDirectory;
    int maxMemoryCacheEntries = 200;
    ThumbnailSize defaultSize = ThumbnailSize::Medium;
    bool generateThumbnails = true;
    std::string thumbnailFormat = "png"; // "png" or "jpg"
    int jpegQuality = 85;
};

class ImageCacheService {
public:
    ImageCacheService();
    ImageCacheService(std::shared_ptr<TrackDatabase> database, std::shared_ptr<TrackMetadata> metadata);
    ~ImageCacheService() = default;

    bool initialize(const ImageCacheConfig& config = {});

    std::optional<CachedImage> getAlbumArt(int64_t trackId, ThumbnailSize size = ThumbnailSize::Medium);
    std::optional<CachedImage> getAlbumArtByPath(const std::string& filePath, ThumbnailSize size = ThumbnailSize::Medium);

    std::vector<uint8_t> getRawAlbumArt(int64_t trackId);

    int precacheAlbumArts(const std::vector<int64_t>& trackIds, ThumbnailSize size = ThumbnailSize::Medium,
                           std::function<void(int, int)> progressCb = nullptr);

    bool invalidate(int64_t trackId);
    int invalidateAll();
    int cleanupOld(int maxAgeDays = 30);

    bool setAlbumArt(int64_t trackId, const std::vector<uint8_t>& imageData);

    ImageCacheStats getStats() const;
    int64_t getMemoryCacheCount() const;
    int64_t getDiskCacheCount() const;

    std::vector<uint8_t> getDefaultAlbumArt(ThumbnailSize size = ThumbnailSize::Medium) const;

private:
    std::string getCacheFilePath(int64_t trackId, ThumbnailSize size) const;
    std::string getDefaultCacheDir() const;
    std::string getCacheKey(int64_t trackId, ThumbnailSize size) const;

    // Simple thumbnail resize (downscale by skipping pixels)
    std::vector<uint8_t> resizeImage(const std::vector<uint8_t>& original, int targetSize) const;

    std::shared_ptr<TrackDatabase> database_;
    std::shared_ptr<TrackMetadata> metadata_;
    ImageCacheConfig config_;
    mutable std::mutex mutex_;

    std::map<std::string, CachedImage> memoryCache_;
    mutable ImageCacheStats stats_;

    bool initialized_ = false;
};

} // namespace BeatMate::Services::Library
