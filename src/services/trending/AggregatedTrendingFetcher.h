#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../streaming/TrendingTracksService.h"

namespace BeatMate::Services::Trending {

struct AggregatedEntry {
    Streaming::PlayCountEntry base;
    uint32_t sourceMask = 0;
    int      sourceCount = 0;      // popcount(sourceMask)
    double   aggregateScore = 0.0; // presenceAcrossSources * recency
    std::string previewUrl;        // iTunes 30s preview when enriched
    int      durationMs = 0;
    int64_t  fetchedAtUnixMs = 0;  // when the data was pulled from the network
};

enum SourceFlag : uint32_t {
    SRC_AppleMusicRSS = 1u << 0,
    SRC_ItunesLegacy  = 1u << 1,
    SRC_Deezer        = 1u << 2,
    SRC_SpotifyGlobal = 1u << 3,
    SRC_SoundCloud    = 1u << 5,
    SRC_Spotify       = 1u << 6,
};

struct FetchOptions {
    std::string country = "us";     // ISO 2-letter code, lowercased
    std::string genre   = "";        // empty = all ; otherwise a Deezer genre key
    bool electronicOnly = false;     // drop rows whose genre is not DJ-friendly
    bool enrichPreview  = true;      // hit iTunes search for missing preview/duration
    int  maxEntries     = 200;
    int  httpTimeoutMs  = 6000;
    int  cacheTtlSec    = 600;       // 10 min — charts update hourly upstream
    bool forceRefresh   = false;     // bypass cache for this call (refresh button)
};

class AggregatedTrendingFetcher {
public:
    AggregatedTrendingFetcher();
    ~AggregatedTrendingFetcher() = default;

    std::vector<AggregatedEntry> fetch(const FetchOptions& opts);

    static std::vector<Streaming::PlayCountEntry>
        toPlayCountEntries(const std::vector<AggregatedEntry>& agg);

    void clearCache();

    static const char* sourceName(uint32_t flag);

private:
    struct CacheBucket {
        std::chrono::steady_clock::time_point at;
        std::vector<AggregatedEntry>          data;
    };
    std::mutex cacheMutex_;
    std::unordered_map<std::string, CacheBucket> cache_;
};

} // namespace BeatMate::Services::Trending
