#include "WeightedScoringService.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <chrono>

namespace BeatMate::Services::Preparation {

WeightedTrackScore WeightedScoringService::scoreTrackPair(
    const Models::Track& reference, const Models::Track& candidate, const ScoringWeights& weights) {

    WeightedTrackScore result;
    result.track = candidate;

    auto addCriterion = [&](const std::string& name, float raw, float weight) {
        CriterionScore cs;
        cs.name = name;
        cs.rawScore = raw;
        cs.weight = weight;
        cs.weightedScore = raw * weight;
        result.criteria.push_back(cs);
        result.totalScore += cs.weightedScore;
    };

    addCriterion("BPM", scoreBpm(reference, candidate), weights.bpm);
    addCriterion("Key", scoreKey(reference, candidate), weights.key);
    addCriterion("Energy", scoreEnergy(reference, candidate), weights.energy);
    addCriterion("Genre", scoreGenre(reference, candidate), weights.genre);
    addCriterion("Mood", scoreMood(reference, candidate), weights.mood);
    addCriterion("Danceability", scoreDanceability(reference, candidate), weights.danceability);
    addCriterion("Popularity", scorePopularity(candidate), weights.popularity);
    addCriterion("Recency", scoreRecency(candidate), weights.recency);

    result.rank = rankFromScore(result.totalScore);
    return result;
}

ScoringResult WeightedScoringService::scoreAll(
    const Models::Track& reference, const std::vector<Models::Track>& candidates, const ScoringWeights& weights) {

    ScoringResult result;
    result.usedWeights = weights;
    result.bestScore = 0.0f;
    result.worstScore = 1.0f;
    float totalScore = 0.0f;

    for (const auto& cand : candidates) {
        if (cand.id == reference.id) continue;
        auto scored = scoreTrackPair(reference, cand, weights);
        result.rankedTracks.push_back(scored);
        totalScore += scored.totalScore;
        result.bestScore = std::max(result.bestScore, scored.totalScore);
        result.worstScore = std::min(result.worstScore, scored.totalScore);
    }

    std::sort(result.rankedTracks.begin(), result.rankedTracks.end(),
              [](const auto& a, const auto& b) { return a.totalScore > b.totalScore; });

    result.avgScore = !result.rankedTracks.empty() ?
        totalScore / static_cast<float>(result.rankedTracks.size()) : 0.0f;

    spdlog::info("WeightedScoringService: Scored {} candidates, best={:.2f}, avg={:.2f}",
                 result.rankedTracks.size(), result.bestScore, result.avgScore);
    return result;
}

ScoringResult WeightedScoringService::scoreAllDefault(
    const Models::Track& reference, const std::vector<Models::Track>& candidates) {
    return scoreAll(reference, candidates, presetBalanced());
}

ScoringWeights WeightedScoringService::presetHarmonic() {
    ScoringWeights w; w.bpm = 0.15f; w.key = 0.5f; w.energy = 0.15f; w.genre = 0.1f;
    w.mood = 0.05f; w.danceability = 0.02f; w.popularity = 0.01f; w.recency = 0.02f;
    return w;
}

ScoringWeights WeightedScoringService::presetEnergy() {
    ScoringWeights w; w.bpm = 0.15f; w.key = 0.1f; w.energy = 0.5f; w.genre = 0.1f;
    w.mood = 0.05f; w.danceability = 0.05f; w.popularity = 0.02f; w.recency = 0.03f;
    return w;
}

ScoringWeights WeightedScoringService::presetBalanced() {
    return ScoringWeights{};
}

ScoringWeights WeightedScoringService::presetDJ() {
    ScoringWeights w; w.bpm = 0.3f; w.key = 0.3f; w.energy = 0.2f; w.genre = 0.1f;
    w.mood = 0.02f; w.danceability = 0.03f; w.popularity = 0.03f; w.recency = 0.02f;
    return w;
}

float WeightedScoringService::scoreBpm(const Models::Track& ref, const Models::Track& cand) const {
    if (ref.bpm <= 0 || cand.bpm <= 0) return 0.5f;
    double diff = std::abs(ref.bpm - cand.bpm);
    if (diff <= 2.0) return 1.0f;
    if (diff <= 4.0) return 0.85f;
    if (diff <= 8.0) return 0.6f;
    double halfDiff = std::abs(ref.bpm - cand.bpm * 2.0);
    if (halfDiff <= 4.0) return 0.7f;
    return std::max(0.0f, static_cast<float>(1.0 - diff / 30.0));
}

float WeightedScoringService::scoreKey(const Models::Track& ref, const Models::Track& cand) const {
    std::string key1 = ref.camelotKey.empty() ? ref.key : ref.camelotKey;
    std::string key2 = cand.camelotKey.empty() ? cand.key : cand.camelotKey;
    if (key1.empty() || key2.empty()) return 0.5f;
    if (key1 == key2) return 1.0f;
    if (key1.size() < 2 || key2.size() < 2) return 0.0f;
    try {
        int num1 = std::stoi(key1.substr(0, key1.size() - 1));
        int num2 = std::stoi(key2.substr(0, key2.size() - 1));
        char let1 = key1.back(); char let2 = key2.back();
        if (num1 == num2 && let1 != let2) return 0.85f;
        int diff = ((num2 - num1) + 12) % 12;
        if ((diff == 1 || diff == 11) && let1 == let2) return 0.9f;
        if ((diff == 2 || diff == 10) && let1 == let2) return 0.5f;
    } catch (...) {}
    return 0.0f;
}

float WeightedScoringService::scoreEnergy(const Models::Track& ref, const Models::Track& cand) const {
    float diff = std::abs(ref.energy - cand.energy);
    return std::max(0.0f, 1.0f - diff / 5.0f);
}

float WeightedScoringService::scoreGenre(const Models::Track& ref, const Models::Track& cand) const {
    if (ref.genre.empty() || cand.genre.empty()) return 0.5f;
    return (ref.genre == cand.genre) ? 1.0f : 0.2f;
}

float WeightedScoringService::scoreMood(const Models::Track& ref, const Models::Track& cand) const {
    if (ref.mood.empty() || cand.mood.empty()) return 0.5f;
    return (ref.mood == cand.mood) ? 1.0f : 0.3f;
}

float WeightedScoringService::scoreDanceability(const Models::Track& ref, const Models::Track& cand) const {
    float diff = std::abs(ref.danceability - cand.danceability);
    return std::max(0.0f, 1.0f - diff);
}

float WeightedScoringService::scorePopularity(const Models::Track& cand) const {
    return std::min(1.0f, static_cast<float>(cand.playCount) / 50.0f);
}

float WeightedScoringService::scoreRecency(const Models::Track& cand) const {
    if (cand.lastPlayed <= 0) return 0.3f;
    auto now = std::chrono::system_clock::now();
    auto nowSec = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    double daysSince = static_cast<double>(nowSec - cand.lastPlayed) / 86400.0;
    return std::max(0.0f, std::min(1.0f, static_cast<float>(1.0 - daysSince / 365.0)));
}

std::string WeightedScoringService::rankFromScore(float score) const {
    if (score >= 0.85f) return "S";
    if (score >= 0.7f) return "A";
    if (score >= 0.55f) return "B";
    if (score >= 0.4f) return "C";
    if (score >= 0.25f) return "D";
    return "F";
}

}
