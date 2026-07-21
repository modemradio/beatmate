#pragma once
#include <cstdint>

#include <string>
#include <vector>
#include <map>
#include <memory>

#include <spdlog/spdlog.h>

namespace BeatMate::Services::Library {

class TrackDatabase;

struct StatEntry {
    std::string category;
    int count = 0;
    float percentage = 0.0f;
};

/// Detailed collection statistics report
struct CollectionReport {
    int64_t totalTracks = 0;
    double totalDurationHours = 0.0;
    int64_t totalSizeBytes = 0;
    std::string totalSizeFormatted;

    double averageBPM = 0.0;
    double averageDuration = 0.0;     // seconds
    float averageEnergy = 0.0f;
    float averageRating = 0.0f;
    float averageDanceability = 0.0f;
    int averageBitRate = 0;

    double minBPM = 0.0;
    double maxBPM = 0.0;
    double minDuration = 0.0;
    double maxDuration = 0.0;
    float minEnergy = 0.0f;
    float maxEnergy = 0.0f;
    int oldestYear = 0;
    int newestYear = 0;

    int analyzedCount = 0;
    int unanalyzedCount = 0;
    int ratedCount = 0;
    int unratedCount = 0;
    int withBPM = 0;
    int withKey = 0;
    int withGenre = 0;
    int withMood = 0;
    int withAlbumArt = 0;
    int neverPlayed = 0;
    int playedAtLeastOnce = 0;

    std::vector<StatEntry> genreDistribution;
    std::vector<StatEntry> bpmDistribution;
    std::vector<StatEntry> keyDistribution;
    std::vector<StatEntry> formatDistribution;
    std::vector<StatEntry> yearDistribution;
    std::vector<StatEntry> ratingDistribution;
    std::vector<StatEntry> energyDistribution;
    std::vector<StatEntry> sourceDistribution;
    std::vector<StatEntry> moodDistribution;
    std::vector<StatEntry> danceabilityDistribution;
    std::vector<StatEntry> bitRateDistribution;
    std::vector<StatEntry> sampleRateDistribution;

    std::vector<StatEntry> topArtists;
    std::vector<StatEntry> topAlbums;

    std::vector<StatEntry> addedByMonth;      // tracks added per month
    std::vector<StatEntry> playsByDayOfWeek;   // play counts by day of week

    float metadataCompleteness = 0.0f;  // 0-100%
};

class CollectionStats {
public:
    explicit CollectionStats(std::shared_ptr<TrackDatabase> database);
    ~CollectionStats() = default;

    int64_t getTotalTracks();
    double getTotalDuration(); // in hours
    int64_t getTotalSize();    // in bytes

    std::vector<StatEntry> getGenreDistribution();
    std::vector<StatEntry> getBPMDistribution(double step = 10.0);
    std::vector<StatEntry> getKeyDistribution();
    std::vector<StatEntry> getFormatDistribution();
    std::vector<StatEntry> getYearDistribution();
    std::vector<StatEntry> getRatingDistribution();
    std::vector<StatEntry> getEnergyDistribution(float step = 1.0f);
    std::vector<StatEntry> getSourceDistribution();

    std::vector<StatEntry> getMoodDistribution();
    std::vector<StatEntry> getDanceabilityDistribution(float step = 0.1f);
    std::vector<StatEntry> getBitRateDistribution();
    std::vector<StatEntry> getSampleRateDistribution();
    std::vector<StatEntry> getTopArtists(int limit = 20);
    std::vector<StatEntry> getTopAlbums(int limit = 20);
    std::vector<StatEntry> getAddedByMonth();
    std::vector<StatEntry> getPlaysByDayOfWeek();

    double getAverageBPM();
    double getAverageDuration();
    float getAverageEnergy();
    float getAverageRating();
    float getAverageDanceability();
    int getAverageBitRate();

    double getMinBPM();
    double getMaxBPM();

    int getAnalyzedCount();
    int getRatedCount();
    int getTracksWithBPM();
    int getTracksWithKey();
    int getTracksWithGenre();
    int getTracksWithMood();
    int getNeverPlayedCount();

    // Metadata completeness (0-100%)
    float getMetadataCompleteness();

    CollectionReport generateFullReport();

    static std::string formatBytes(int64_t bytes);
    static std::string formatDuration(double seconds);

private:
    std::shared_ptr<TrackDatabase> database_;
};

} // namespace BeatMate::Services::Library
