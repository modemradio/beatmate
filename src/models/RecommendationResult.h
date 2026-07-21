#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

struct MatchedCriteria {
    bool keyCompatible = false;
    bool bpmInRange = false;
    bool energyMatched = false;
    bool genreCompatible = false;
    bool moodMatched = false;
    bool danceabilityMatched = false;

    std::string keyRelation;
    double bpmDifference = 0.0;
    float energyDifference = 0.0f;
    float genreSimilarity = 0.0f;
    float overallSimilarity = 0.0f;

    MatchedCriteria() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(MatchedCriteria,
        keyCompatible, bpmInRange, energyMatched, genreCompatible,
        moodMatched, danceabilityMatched,
        keyRelation, bpmDifference, energyDifference,
        genreSimilarity, overallSimilarity
    )
};

struct RecommendationResult {
    int64_t trackId = 0;
    float score = 0.0f;
    std::vector<std::string> reasons;

    MatchedCriteria matchedCriteria;

    std::string title;
    std::string artist;
    double bpm = 0.0;
    std::string key;
    float energy = 0.0f;
    std::string genre;

    std::string algorithm;

    RecommendationResult() = default;

    RecommendationResult(int64_t trackId, float score)
        : trackId(trackId), score(score) {}

    bool operator==(const RecommendationResult& other) const { return trackId == other.trackId; }

    bool operator<(const RecommendationResult& other) const {
        return score > other.score;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(RecommendationResult,
        trackId, score, reasons, matchedCriteria,
        title, artist, bpm, key, energy, genre, algorithm
    )
};

struct RecommendationRequest {
    int64_t referenceTrackId = 0;
    int maxResults = 20;

    float bpmWeight = 0.3f;
    float keyWeight = 0.3f;
    float energyWeight = 0.2f;
    float genreWeight = 0.1f;
    float moodWeight = 0.1f;

    double bpmTolerance = 6.0;
    float energyTolerance = 2.0f;
    bool onlyCompatibleKeys = true;
    std::vector<std::string> excludeTrackIds;

    std::string energyDirection;

    RecommendationRequest() = default;

    explicit RecommendationRequest(int64_t referenceTrackId)
        : referenceTrackId(referenceTrackId) {}

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(RecommendationRequest,
        referenceTrackId, maxResults,
        bpmWeight, keyWeight, energyWeight, genreWeight, moodWeight,
        bpmTolerance, energyTolerance, onlyCompatibleKeys, excludeTrackIds,
        energyDirection
    )
};

}
