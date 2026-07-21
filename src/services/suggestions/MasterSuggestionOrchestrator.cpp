#include "MasterSuggestionOrchestrator.h"
#include "MyStyleModel.h"
#include "../library/TrackDatabase.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace BeatMate::Services::Suggestions {

MasterSuggestionOrchestrator::MasterSuggestionOrchestrator(std::shared_ptr<Library::TrackDatabase> database)
    : database_(database) {
    intelligentEngine_ = std::make_unique<IntelligentSuggestionEngine>(database);
    hyperEngine_ = std::make_unique<HyperIntelligentSuggestionEngine>(database);
    proEngine_ = std::make_unique<UltraProSuggestionEngine>(database);
    quantumEngine_ = std::make_unique<QuantumSuggestionEngine>(database);
}

std::vector<MasterSuggestion> MasterSuggestionOrchestrator::orchestrate(const Models::Track& current, int count) {
    return orchestrateWithConfig(current, config_);
}

std::vector<MasterSuggestion> MasterSuggestionOrchestrator::orchestrateWithConfig(
    const Models::Track& current, const MasterConfig& config) {

    int fetchCount = config.maxResults * 2;
    std::map<std::string, std::vector<std::pair<int64_t, float>>> engineResults;
    std::map<int64_t, Models::Track> trackMap;

    if (config.enableAll || config.intelligentWeight > 0) {
        auto results = intelligentEngine_->suggest(current, fetchCount);
        std::vector<std::pair<int64_t, float>> scores;
        for (const auto& r : results) {
            scores.push_back({r.track.id, r.score * config.intelligentWeight});
            trackMap[r.track.id] = r.track;
        }
        engineResults["intelligent"] = scores;
    }

    if (config.enableAll || config.hyperWeight > 0) {
        auto results = hyperEngine_->suggest(current, fetchCount);
        std::vector<std::pair<int64_t, float>> scores;
        for (const auto& r : results) {
            scores.push_back({r.track.id, r.score * config.hyperWeight});
            trackMap[r.track.id] = r.track;
        }
        engineResults["hyper"] = scores;
    }

    if (config.enableAll || config.proWeight > 0) {
        auto results = proEngine_->suggest(current, fetchCount);
        std::vector<std::pair<int64_t, float>> scores;
        for (const auto& r : results) {
            scores.push_back({r.track.id, r.score * config.proWeight});
            trackMap[r.track.id] = r.track;
        }
        engineResults["pro"] = scores;
    }

    if (config.enableAll || config.quantumWeight > 0) {
        auto results = quantumEngine_->suggest(current, fetchCount);
        std::vector<std::pair<int64_t, float>> scores;
        for (const auto& r : results) {
            scores.push_back({r.track.id, r.state.collapsedScore * config.quantumWeight});
            trackMap[r.track.id] = r.track;
        }
        engineResults["quantum"] = scores;
    }

    auto merged = mergeEngineResults(engineResults, trackMap);

    // Apply MyStyleModel prior (personalization). If the model is unset or the
    if (myStyle_ != nullptr && myStyleWeight_ > 0.0f
        && myStyle_->hasEnoughHistory()) {
        for (auto& m : merged) {
            float prior = myStyle_->scoreCandidate(current, m.track);
            // Blend: final' = final * (1 - w + w * prior).
            float w = myStyleWeight_;
            if (w < 0.0f) w = 0.0f;
            if (w > 1.0f) w = 1.0f;
            float mult = (1.0f - w) + w * prior;
            m.finalScore *= mult;
            m.engineScores["myStylePrior"] = prior;
        }
        // Re-sort after the prior perturbs the ranking.
        std::sort(merged.begin(), merged.end(),
                  [](const auto& a, const auto& b) { return a.finalScore > b.finalScore; });
    }

    if (static_cast<int>(merged.size()) > config.maxResults) {
        merged.resize(static_cast<size_t>(config.maxResults));
    }

    spdlog::info("MasterSuggestionOrchestrator: {} master suggestions for '{}' (myStylePrior={})",
                 merged.size(), current.title,
                 (myStyle_ && myStyle_->hasEnoughHistory()) ? "on" : "off");
    return merged;
}

std::vector<MasterSuggestion> MasterSuggestionOrchestrator::mergeEngineResults(
    const std::map<std::string, std::vector<std::pair<int64_t, float>>>& engineResults,
    const std::map<int64_t, Models::Track>& trackMap) const {

    std::map<int64_t, MasterSuggestion> aggregated;

    for (const auto& [engineName, results] : engineResults) {
        for (const auto& [trackId, score] : results) {
            auto& sug = aggregated[trackId];
            if (sug.track.id == 0) {
                auto it = trackMap.find(trackId);
                if (it != trackMap.end()) sug.track = it->second;
            }
            sug.engineScores[engineName] = score;
            sug.finalScore += score;
        }
    }

    std::vector<MasterSuggestion> result;
    for (auto& [id, sug] : aggregated) {
        int enginesAgreeing = 0;
        float maxScore = 0.0f;
        std::string bestEngine;
        for (const auto& [engine, score] : sug.engineScores) {
            if (score > 0.1f) ++enginesAgreeing;
            if (score > maxScore) { maxScore = score; bestEngine = engine; }
        }
        sug.consensus = static_cast<float>(enginesAgreeing) / static_cast<float>(engineResults.size());
        sug.bestEngine = bestEngine;

        if (sug.consensus > 0.75f) sug.recommendation = "Highly recommended by all engines";
        else if (sug.consensus > 0.5f) sug.recommendation = "Recommended by most engines";
        else sug.recommendation = "Suggested by " + bestEngine + " engine";

        result.push_back(sug);
    }

    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.finalScore > b.finalScore; });
    return result;
}

} // namespace BeatMate::Services::Suggestions
