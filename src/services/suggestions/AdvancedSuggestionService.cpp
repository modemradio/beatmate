#include "AdvancedSuggestionService.h"
#include "../library/TrackDatabase.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace BeatMate::Services::Suggestions {

AdvancedSuggestionService::AdvancedSuggestionService(std::shared_ptr<Library::TrackDatabase> database)
    : database_(std::move(database)) {}

std::vector<AdvancedResult> AdvancedSuggestionService::suggest(
    const Models::Track& current, const AdvancedSuggestionConfig& config) {

    if (!database_) return {};
    auto allTracks = database_->getAllTracks();
    std::vector<AdvancedResult> results;

    for (const auto& candidate : allTracks) {
        if (candidate.id == current.id) continue;
        auto result = analyzeCandidate(current, candidate, config);
        if (result.base.score >= config.minScore) results.push_back(result);
    }

    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) { return a.base.score > b.base.score; });
    if (static_cast<int>(results.size()) > config.maxResults) results.resize(static_cast<size_t>(config.maxResults));

    spdlog::info("AdvancedSuggestionService: {} suggestions for '{}'", results.size(), current.title);
    return results;
}

std::vector<AdvancedResult> AdvancedSuggestionService::suggestForPlaylist(
    const std::vector<Models::Track>& playlist, int count) {

    if (playlist.empty() || !database_) return {};

    const auto& lastTrack = playlist.back();
    auto results = suggest(lastTrack);

    for (auto& r : results) {
        for (const auto& pt : playlist) {
            if (r.base.track.id == pt.id) { r.base.score = 0.0f; break; }
            if (r.base.track.artist == pt.artist) r.base.score *= 0.8f;
        }
    }

    results.erase(std::remove_if(results.begin(), results.end(),
                                  [](const auto& r) { return r.base.score <= 0.0f; }), results.end());
    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) { return a.base.score > b.base.score; });
    if (static_cast<int>(results.size()) > count) results.resize(static_cast<size_t>(count));
    return results;
}

std::vector<AdvancedResult> AdvancedSuggestionService::suggestSimilar(const Models::Track& reference, int count) {
    AdvancedSuggestionConfig config;
    config.useHarmonicAnalysis = false;
    config.useEnergyFlow = true;
    config.useGenreMatching = true;
    config.useMoodAnalysis = true;
    config.maxResults = count;
    return suggest(reference, config);
}

AdvancedResult AdvancedSuggestionService::analyzeCandidate(
    const Models::Track& current, const Models::Track& candidate,
    const AdvancedSuggestionConfig& config) const {

    AdvancedResult result;
    result.base.track = candidate;
    float totalWeight = 0.0f;
    float totalScore = 0.0f;

    if (config.useHarmonicAnalysis) {
        std::string key1 = current.camelotKey.empty() ? current.key : current.camelotKey;
        std::string key2 = candidate.camelotKey.empty() ? candidate.key : candidate.camelotKey;
        if (key1 == key2) result.harmonicQuality = 1.0f;
        else if (isKeyCompatible(key1, key2)) result.harmonicQuality = 0.85f;
        else result.harmonicQuality = 0.1f;

        if (current.bpm > 0 && candidate.bpm > 0) {
            float bpmFit = std::max(0.0f, static_cast<float>(1.0 - std::abs(current.bpm - candidate.bpm) / 10.0));
            result.harmonicQuality = result.harmonicQuality * 0.6f + bpmFit * 0.4f;
        }
        totalScore += result.harmonicQuality * 0.35f;
        totalWeight += 0.35f;
    }

    if (config.useEnergyFlow) {
        result.energyFlowQuality = std::max(0.0f, 1.0f - std::abs(current.energy - candidate.energy) / 5.0f);
        totalScore += result.energyFlowQuality * 0.25f;
        totalWeight += 0.25f;
    }

    if (config.useGenreMatching) {
        result.genreMatch = (!current.genre.empty() && current.genre == candidate.genre) ? 1.0f : 0.2f;
        totalScore += result.genreMatch * 0.2f;
        totalWeight += 0.2f;
    }

    if (config.useMoodAnalysis) {
        result.moodMatch = (!current.mood.empty() && current.mood == candidate.mood) ? 1.0f : 0.3f;
        totalScore += result.moodMatch * 0.1f;
        totalWeight += 0.1f;
    }

    if (config.useTemporalContext) {
        float freshness = std::min(1.0f, 1.0f - static_cast<float>(candidate.playCount) / 100.0f);
        totalScore += freshness * 0.1f;
        totalWeight += 0.1f;
    }

    result.base.score = totalWeight > 0 ? totalScore / totalWeight : 0.0f;

    if (result.harmonicQuality > 0.8f) result.analysis = "Strong harmonic compatibility";
    else if (result.energyFlowQuality > 0.8f) result.analysis = "Smooth energy flow";
    else if (result.genreMatch > 0.8f) result.analysis = "Same genre family";
    else result.analysis = "Multi-factor match";

    result.base.reason = result.analysis;
    result.base.componentScores["harmonic"] = result.harmonicQuality;
    result.base.componentScores["energy"] = result.energyFlowQuality;
    result.base.componentScores["genre"] = result.genreMatch;

    return result;
}

bool AdvancedSuggestionService::isKeyCompatible(const std::string& key1, const std::string& key2) const {
    if (key1.size() < 2 || key2.size() < 2) return false;
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

}
