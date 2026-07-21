#pragma once

#include "AdvancedColouredWaveformService.h"
#include <string>
#include <mutex>

namespace BeatMate::Core {

class WaveformCacheService {
public:
    WaveformCacheService();
    ~WaveformCacheService();

    bool save(const std::string& trackId, const ColouredWaveformData& data,
              const std::string& cacheDir = "WaveformCache");

    bool load(const std::string& trackId, ColouredWaveformData& data,
              const std::string& cacheDir = "WaveformCache");

    bool exists(const std::string& trackId, const std::string& cacheDir = "WaveformCache");

    bool remove(const std::string& trackId, const std::string& cacheDir = "WaveformCache");

    void clearAll(const std::string& cacheDir = "WaveformCache");

    size_t getCacheSize(const std::string& cacheDir = "WaveformCache");

    void setMaxCacheSize(size_t bytes) { maxCacheSize_ = bytes; }

private:
    std::string getCachePath(const std::string& trackId, const std::string& cacheDir);
    std::string sanitizeFilename(const std::string& trackId);
    void enforceMaxCacheSize(const std::string& cacheDir);

    size_t maxCacheSize_ = 1024 * 1024 * 512;
    mutable std::mutex mutex_;

    static constexpr uint32_t MAGIC = 0x424D504B;
    static constexpr uint32_t VERSION = 2;
};

}
