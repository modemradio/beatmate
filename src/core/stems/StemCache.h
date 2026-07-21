#pragma once
#include "StemSeparator.h"
#include <optional>
#include <string>

namespace BeatMate::Core {

class StemCache {
public:
    explicit StemCache(const std::string& cacheDir = "StemsCache");
    ~StemCache();

    bool save(const std::string& trackId, const StemResult& stems);
    std::optional<StemResult> load(const std::string& trackId);
    bool exists(const std::string& trackId) const;
    void remove(const std::string& trackId);
    void clear();
    int64_t getCacheSizeBytes() const;

    void setMaxCacheSize(int64_t bytes) { maxCacheBytes_ = bytes; }
    void setExpirationDays(int days) { expirationDays_ = days; }

private:
    void evictOldEntries();
    std::string cacheDir_;
    bool ready_ = false;
    int64_t maxCacheBytes_ = 10LL * 1024 * 1024 * 1024; // 10GB
    int expirationDays_ = 30;
};

} // namespace BeatMate::Core
