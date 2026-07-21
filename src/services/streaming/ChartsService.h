#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

namespace BeatMate::Services::Streaming {

struct LiveChartEntry {
    int position = 0;
    std::string title;
    std::string artist;
    std::string artworkUrl;
    std::string previewUrl;
    int previousPosition = 0;
    int delta = 0;
    bool isNew = false;
};

struct LiveChart {
    std::string country;
    std::string source;
    std::string chartDate;
    std::string errorMessage;
    int64_t fetchedAtMs = 0;
    bool fromCache = false;
    std::vector<LiveChartEntry> entries;
};

struct SimilarStreamingTrack {
    std::string title;
    std::string artist;
    std::string reason;
    std::string previewUrl;
    std::string artworkUrl;
    bool sameArtist = false;
};

class ChartsService {
public:
    static ChartsService& instance();

    static constexpr int kRefreshAfterHours = 3;

    LiveChart getChart(const std::string& country, bool forceRefresh = false);
    LiveChart getCachedChart(const std::string& country);

    std::vector<SimilarStreamingTrack> getSimilarTracks(const std::string& title,
                                                        const std::string& artist,
                                                        int maxResults = 40);

    static std::string normalizeKey(const std::string& text);
    static std::string matchKey(const std::string& title, const std::string& artist);

private:
    ChartsService() = default;

    juce::File cacheFileFor(const std::string& country) const;
    LiveChart readCacheLocked(const std::string& country);
    void writeCacheLocked(const LiveChart& chart);

    static bool fetchDeezer(const std::string& country, LiveChart& out);
    static bool fetchItunesRss(const std::string& country, LiveChart& out);
    static bool fetchKworb(const std::string& country, LiveChart& out);
    static void carryDeltas(LiveChart& fresh, const LiveChart& previous);

    std::mutex mutex_;
    std::map<std::string, LiveChart> memCache_;
};

} // namespace BeatMate::Services::Streaming
