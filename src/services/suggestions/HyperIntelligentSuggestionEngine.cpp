#include "HyperIntelligentSuggestionEngine.h"
#include "../library/TrackDatabase.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace BeatMate::Services::Suggestions {

HyperIntelligentSuggestionEngine::HyperIntelligentSuggestionEngine(std::shared_ptr<Library::TrackDatabase> database)
    : database_(std::move(database)) {}

std::vector<HyperSuggestion> HyperIntelligentSuggestionEngine::suggest(const Models::Track& current, int count) {
    if (!database_) return {};
    auto allTracks = database_->getAllTracks();
    std::vector<HyperSuggestion> results;

    for (const auto& candidate : allTracks) {
        if (candidate.id == current.id) continue;

        HyperSuggestion sug;
        sug.track = candidate;
        sug.factors.spectralSimilarity = spectralSimilarity(current, candidate);
        sug.factors.rhythmicSimilarity = rhythmicSimilarity(current, candidate);
        sug.factors.structuralCompatibility = structuralCompatibility(current, candidate);
        sug.factors.timbralMatch = timbralMatch(current, candidate);
        sug.factors.historicalSuccess = historicalSuccess(candidate);

        sug.score = sug.factors.spectralSimilarity * 0.2f +
                    sug.factors.rhythmicSimilarity * 0.25f +
                    sug.factors.structuralCompatibility * 0.2f +
                    sug.factors.timbralMatch * 0.15f +
                    sug.factors.historicalSuccess * 0.2f;

        sug.confidence = std::min(1.0f, (sug.factors.spectralSimilarity + sug.factors.rhythmicSimilarity) / 2.0f);
        sug.insight = generateInsight(sug.factors);

        if (sug.score > 0.2f) results.push_back(sug);
    }

    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    if (static_cast<int>(results.size()) > count) results.resize(static_cast<size_t>(count));
    spdlog::info("HyperIntelligentSuggestionEngine: {} deep suggestions for '{}'", results.size(), current.title);
    return results;
}

std::vector<HyperSuggestion> HyperIntelligentSuggestionEngine::deepAnalyze(
    const Models::Track& current, const Models::TrackAnalysis& analysis, int count) {
    auto results = suggest(current, count * 2);

    for (auto& sug : results) {
        if (analysis.energy > 0) {
            float energyMatch = 1.0f - std::abs(sug.track.energy - analysis.energy * 10.0f) / 10.0f;
            sug.score = sug.score * 0.7f + energyMatch * 0.3f;
        }
        if (analysis.danceability > 0) {
            float danceMatch = 1.0f - std::abs(sug.track.danceability - analysis.danceability);
            sug.score = sug.score * 0.8f + danceMatch * 0.2f;
        }
    }

    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    if (static_cast<int>(results.size()) > count) results.resize(static_cast<size_t>(count));
    return results;
}

float HyperIntelligentSuggestionEngine::spectralSimilarity(const Models::Track& a, const Models::Track& b) const {
    // Approximate spectral similarity from danceability and energy
    float daDiff = std::abs(a.danceability - b.danceability);
    float eDiff = std::abs(a.energy - b.energy) / 10.0f;
    return std::max(0.0f, 1.0f - (daDiff + eDiff) / 2.0f);
}

float HyperIntelligentSuggestionEngine::rhythmicSimilarity(const Models::Track& a, const Models::Track& b) const {
    if (a.bpm <= 0 || b.bpm <= 0) return 0.5f;
    double ratio = std::min(a.bpm, b.bpm) / std::max(a.bpm, b.bpm);
    if (ratio > 0.94) return 1.0f;
    if (ratio > 0.88) return 0.7f;
    double halfRatio = std::min(a.bpm, b.bpm * 2.0) / std::max(a.bpm, b.bpm * 2.0);
    if (halfRatio > 0.94) return 0.8f;
    return std::max(0.0f, static_cast<float>(ratio));
}

float HyperIntelligentSuggestionEngine::structuralCompatibility(const Models::Track& a, const Models::Track& b) const {
    std::string key1 = a.camelotKey.empty() ? a.key : a.camelotKey;
    std::string key2 = b.camelotKey.empty() ? b.key : b.camelotKey;
    float keyScore = 0.0f;
    if (key1 == key2) keyScore = 1.0f;
    else if (isKeyCompatible(key1, key2)) keyScore = 0.85f;

    // Duration similarity (similar length tracks mix better)
    float durRatio = 0.5f;
    if (a.duration > 0 && b.duration > 0) {
        durRatio = static_cast<float>(std::min(a.duration, b.duration) / std::max(a.duration, b.duration));
    }

    return keyScore * 0.7f + durRatio * 0.3f;
}

float HyperIntelligentSuggestionEngine::timbralMatch(const Models::Track& a, const Models::Track& b) const {
    float score = 0.5f;
    if (a.genre == b.genre && !a.genre.empty()) score += 0.3f;
    if (a.mood == b.mood && !a.mood.empty()) score += 0.2f;
    return std::min(1.0f, score);
}

float HyperIntelligentSuggestionEngine::historicalSuccess(const Models::Track& candidate) const {
    if (candidate.playCount <= 0) return 0.3f;
    float playScore = std::min(1.0f, static_cast<float>(candidate.playCount) / 20.0f);
    float ratingScore = static_cast<float>(candidate.rating) / 5.0f;
    return playScore * 0.6f + ratingScore * 0.4f;
}

std::string HyperIntelligentSuggestionEngine::generateInsight(const DeepAnalysisFactors& factors) const {
    if (factors.rhythmicSimilarity > 0.9f && factors.structuralCompatibility > 0.8f)
        return "Perfect rhythmic and harmonic match - seamless transition possible";
    if (factors.spectralSimilarity > 0.8f)
        return "Similar sonic character - will blend naturally";
    if (factors.historicalSuccess > 0.8f)
        return "Crowd-tested favorite - high audience approval expected";
    if (factors.timbralMatch > 0.7f)
        return "Timbral compatibility - similar production style";
    return "Reasonable match based on multi-factor analysis";
}

bool HyperIntelligentSuggestionEngine::isKeyCompatible(const std::string& key1, const std::string& key2) const {
    if (key1.empty() || key2.empty() || key1.size() < 2 || key2.size() < 2) return false;
    try {
        int num1 = std::stoi(key1.substr(0, key1.size() - 1));
        int num2 = std::stoi(key2.substr(0, key2.size() - 1));
        char let1 = key1.back(); char let2 = key2.back();
        if (num1 == num2 && let1 != let2) return true;
        int diff = ((num2 - num1) + 12) % 12;
        return (diff == 1 || diff == 11) && let1 == let2;
    } catch (...) {}
    return false;
}

} // namespace BeatMate::Services::Suggestions
