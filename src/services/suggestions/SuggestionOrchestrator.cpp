#include "SuggestionOrchestrator.h"
#include "SmartSuggestionEngine.h"
#include "HarmonicSuggestionEngine.h"
#include "MLSuggestionEngine.h"
#include "HistoryAnalysisEngine.h"
#include "ContextualFilter.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <set>

namespace BeatMate::Services::Suggestions {

SuggestionOrchestrator::SuggestionOrchestrator() = default;
SuggestionOrchestrator::~SuggestionOrchestrator() = default;

std::vector<RecommendationResult> SuggestionOrchestrator::getSuggestions(
    const Models::Track& currentTrack, int count) {

    std::vector<std::vector<RecommendationResult>> allResults;

    if (smartEngine_) {
        auto results = smartEngine_->suggest(currentTrack, count * 2);
        for (auto& r : results) r.score *= weightSmart_;
        allResults.push_back(std::move(results));
    }

    if (harmonicEngine_) {
        auto results = harmonicEngine_->suggest(currentTrack, count * 2);
        for (auto& r : results) r.score *= weightHarmonic_;
        allResults.push_back(std::move(results));
    }

    if (mlEngine_) {
        auto results = mlEngine_->suggest(currentTrack, count * 2);
        for (auto& r : results) r.score *= weightML_;
        allResults.push_back(std::move(results));
    }

    if (historyEngine_) {
        auto results = historyEngine_->suggest(currentTrack, count * 2);
        for (auto& r : results) r.score *= weightHistory_;
        allResults.push_back(std::move(results));
    }

    auto merged = mergeResults(allResults, count);

    // Apply contextual filter if available
    if (contextFilter_) {
        merged = contextFilter_->filter(merged, count);
    }

    spdlog::info("SuggestionOrchestrator: {} suggestions for '{}'", merged.size(), currentTrack.title);
    return merged;
}

void SuggestionOrchestrator::setWeights(float smart, float harmonic, float ml, float history) {
    weightSmart_ = smart;
    weightHarmonic_ = harmonic;
    weightML_ = ml;
    weightHistory_ = history;
}

std::vector<RecommendationResult> SuggestionOrchestrator::mergeResults(
    const std::vector<std::vector<RecommendationResult>>& allResults, int count) {

    // Aggregate scores by track ID
    std::map<int64_t, RecommendationResult> aggregated;

    for (const auto& results : allResults) {
        for (const auto& result : results) {
            auto& agg = aggregated[result.track.id];
            if (agg.track.id == 0) {
                agg = result;
            } else {
                agg.score += result.score;
                for (const auto& [key, val] : result.componentScores) {
                    agg.componentScores[key] = std::max(agg.componentScores[key], val);
                }
            }
        }
    }

    // Sort by score
    std::vector<RecommendationResult> merged;
    for (auto& [id, result] : aggregated) {
        merged.push_back(std::move(result));
    }

    std::sort(merged.begin(), merged.end(),
              [](const auto& a, const auto& b) { return a.score > b.score; });

    if (static_cast<int>(merged.size()) > count) {
        merged.resize(static_cast<size_t>(count));
    }

    return merged;
}

} // namespace BeatMate::Services::Suggestions
