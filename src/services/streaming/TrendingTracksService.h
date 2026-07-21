#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace BeatMate::Services::Streaming {

struct PlayCountEntry {
    int64_t trackId = 0;
    std::string title;
    std::string artist;
    std::string genre;
    double bpm = 0.0;
    std::string key;
    int totalPlays = 0;
    int uniqueUsers = 0;
    int playsLast7Days = 0;
    int playsLast30Days = 0;
    double trendScore = 0.0;
    int64_t lastPlayedAt = 0;
    int64_t firstPlayedAt = 0;
    std::string artworkUrl;
};

enum class TrendingPeriod {
    Daily,
    Weekly,
    Monthly,
    AllTime
};

struct TrendingFilter {
    std::string genre;
    double minBpm = 0.0;
    double maxBpm = 999.0;
    std::string key;
    TrendingPeriod period = TrendingPeriod::Weekly;
};

using TrendingUpdateCallback = std::function<void(const std::vector<PlayCountEntry>& top100)>;

class TrendingTracksService {
public:
    TrendingTracksService();
    ~TrendingTracksService() = default;

    void recordPlay(int64_t trackId, const std::string& title, const std::string& artist,
                    const std::string& genre = "", double bpm = 0.0, const std::string& key = "",
                    int64_t userId = 0);
    void recordPlayBatch(const std::vector<std::pair<int64_t, int64_t>>& trackUserPairs);

    std::vector<PlayCountEntry> getTop100(TrendingPeriod period = TrendingPeriod::Weekly);
    std::vector<PlayCountEntry> getTop100ByGenre(const std::string& genre,
                                                  TrendingPeriod period = TrendingPeriod::Weekly);
    std::vector<PlayCountEntry> getTop100Filtered(const TrendingFilter& filter);

    std::vector<PlayCountEntry> getRisingTracks(int limit = 20);
    std::vector<PlayCountEntry> getNewTrending(int limit = 20);
    std::vector<PlayCountEntry> getFallingTracks(int limit = 20);
    double getTrendScore(int64_t trackId) const;

    int getTotalPlays() const;
    int getTotalUniqueTracks() const;
    int getTotalUniqueUsers() const;
    std::vector<std::pair<std::string, int>> getTopGenres(int limit = 10) const;
    std::vector<std::pair<double, int>> getBpmDistribution(int bucketSize = 5) const;

    void recalculateTrendScores();
    void pruneOldData(int daysToKeep = 90);
    void exportToJson(const std::string& filePath) const;
    bool importFromJson(const std::string& filePath);

    void registerUpdateCallback(TrendingUpdateCallback callback);

    void setRecencyWeight(double weight) { recencyWeight_ = weight; }
    void setVelocityWeight(double weight) { velocityWeight_ = weight; }
    void setUniqueUsersWeight(double weight) { uniqueUsersWeight_ = weight; }

private:
    void computeTrendScore(PlayCountEntry& entry) const;
    void notifyCallbacks();

    mutable std::mutex dataMutex_;
    std::map<int64_t, PlayCountEntry> playData_;

    struct DailyPlayCount {
        int64_t trackId = 0;
        std::string date;
        int plays = 0;
    };
    std::vector<DailyPlayCount> dailyPlays_;

    std::map<int64_t, std::vector<int64_t>> trackUsers_;

    double recencyWeight_ = 0.4;
    double velocityWeight_ = 0.35;
    double uniqueUsersWeight_ = 0.25;

    std::vector<TrendingUpdateCallback> updateCallbacks_;
};

}
