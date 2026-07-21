#pragma once
#include <string>
#include <vector>
#include <map>
#include "../../models/Track.h"

namespace BeatMate::Services::Preparation {

struct ScoringWeights {
    float bpm = 0.25f;
    float key = 0.25f;
    float energy = 0.2f;
    float genre = 0.1f;
    float mood = 0.05f;
    float danceability = 0.05f;
    float popularity = 0.05f;
    float recency = 0.05f;

    void normalize() {
        float total = bpm + key + energy + genre + mood + danceability + popularity + recency;
        if (total > 0) {
            bpm /= total; key /= total; energy /= total; genre /= total;
            mood /= total; danceability /= total; popularity /= total; recency /= total;
        }
    }
};

struct CriterionScore {
    std::string name;
    float rawScore = 0.0f;
    float weight = 0.0f;
    float weightedScore = 0.0f;
    std::string detail;
};

struct WeightedTrackScore {
    Models::Track track;
    float totalScore = 0.0f;
    std::vector<CriterionScore> criteria;
    std::string rank;
};

struct ScoringResult {
    std::vector<WeightedTrackScore> rankedTracks;
    float avgScore = 0.0f;
    float bestScore = 0.0f;
    float worstScore = 0.0f;
    ScoringWeights usedWeights;
};

class WeightedScoringService {
public:
    WeightedScoringService() = default;

    WeightedTrackScore scoreTrackPair(const Models::Track& reference, const Models::Track& candidate, const ScoringWeights& weights);
    ScoringResult scoreAll(const Models::Track& reference, const std::vector<Models::Track>& candidates, const ScoringWeights& weights);
    ScoringResult scoreAllDefault(const Models::Track& reference, const std::vector<Models::Track>& candidates);

    static ScoringWeights presetHarmonic();
    static ScoringWeights presetEnergy();
    static ScoringWeights presetBalanced();
    static ScoringWeights presetDJ();

private:
    float scoreBpm(const Models::Track& ref, const Models::Track& cand) const;
    float scoreKey(const Models::Track& ref, const Models::Track& cand) const;
    float scoreEnergy(const Models::Track& ref, const Models::Track& cand) const;
    float scoreGenre(const Models::Track& ref, const Models::Track& cand) const;
    float scoreMood(const Models::Track& ref, const Models::Track& cand) const;
    float scoreDanceability(const Models::Track& ref, const Models::Track& cand) const;
    float scorePopularity(const Models::Track& cand) const;
    float scoreRecency(const Models::Track& cand) const;
    std::string rankFromScore(float score) const;
};

} // namespace BeatMate::Services::Preparation
