#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <mutex>
#include <cstdint>

#include <spdlog/spdlog.h>

#include "../../models/Track.h"

namespace BeatMate::Services::Library {

class TrackDatabase;

enum class SuggestionType {
    UnratedTracks,
    UnanalyzedTracks,
    PoorMetadata,
    DuplicateSuspect,
    LongUnplayed,
    PopularGenreGap,
    BPMGap,
    KeyGap,
    RecentlyAdded,
    HighEnergy,
    LowEnergy,
    GenreRecommendation,
    DiversityPick
};

struct CollectionSuggestionItem {
    SuggestionType type;
    std::string title;
    std::string description;
    float priority = 0.0f;
    std::vector<Models::Track> tracks;
    int affectedCount = 0;
    std::string actionLabel;
};

struct CollectionHealth {
    int totalTracks = 0;
    int analyzedTracks = 0;
    int ratedTracks = 0;
    int tracksWithBPM = 0;
    int tracksWithKey = 0;
    int tracksWithGenre = 0;
    int tracksWithMood = 0;
    int tracksWithEnergy = 0;
    int missingFiles = 0;
    int neverPlayedTracks = 0;

    float completenessScore = 0.0f;
    float qualityScore = 0.0f;
    std::string overallGrade;
};

class CollectionSuggestionService {
public:
    explicit CollectionSuggestionService(std::shared_ptr<TrackDatabase> database);
    ~CollectionSuggestionService() = default;

    std::vector<CollectionSuggestionItem> getAllSuggestions(int maxPerType = 10);

    CollectionSuggestionItem getUnratedSuggestion(int limit = 20);
    CollectionSuggestionItem getUnanalyzedSuggestion(int limit = 20);
    CollectionSuggestionItem getPoorMetadataSuggestion(int limit = 20);
    CollectionSuggestionItem getLongUnplayedSuggestion(int daysSincePlay = 90, int limit = 20);
    CollectionSuggestionItem getRecentlyAddedUnplayedSuggestion(int limit = 20);
    CollectionSuggestionItem getBPMGapSuggestion();
    CollectionSuggestionItem getKeyGapSuggestion();

    CollectionHealth analyzeCollectionHealth();

    std::map<std::string, int> getGenreDistribution() const;
    std::vector<std::string> getUnderrepresentedGenres(int threshold = 5) const;
    std::vector<std::string> getTopGenres(int count = 10) const;

    std::vector<Models::Track> getDiscoveryPicks(int count = 10);
    std::vector<Models::Track> getRediscoveryPicks(int count = 10);
    std::vector<Models::Track> getFavoritePredictions(int count = 10);

private:
    std::string gradeFromScore(float score) const;

    std::shared_ptr<TrackDatabase> database_;
    mutable std::mutex mutex_;
};

}
