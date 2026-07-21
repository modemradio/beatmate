#include "ContextualFilter.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace BeatMate::Services::Suggestions {

std::vector<RecommendationResult> ContextualFilter::filter(
    const std::vector<RecommendationResult>& results, int count) {
    std::vector<RecommendationResult> filtered;

    for (auto result : results) {
        float ctxScore = contextScore(result.track);
        result.score *= (0.5f + 0.5f * ctxScore);
        result.componentScores["context"] = ctxScore;
        filtered.push_back(result);
    }

    std::sort(filtered.begin(), filtered.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    if (static_cast<int>(filtered.size()) > count) filtered.resize(static_cast<size_t>(count));
    return filtered;
}

float ContextualFilter::contextScore(const Models::Track& track) const {
    float score = 1.0f;

    float energyDiff = std::abs(track.energy - context_.targetEnergy);
    score *= std::max(0.0f, 1.0f - energyDiff / 10.0f);

    if (!context_.preferredGenre.empty() && !track.genre.empty()) {
        score *= (track.genre == context_.preferredGenre) ? 1.2f : 0.8f;
    }

    if (context_.hourOfDay >= 2 && context_.hourOfDay <= 6) {
        if (track.energy < 5.0f) score *= 1.1f;
    } else if (context_.hourOfDay >= 23 || context_.hourOfDay <= 1) {
        if (track.energy > 7.0f) score *= 1.1f;
    }

    return std::clamp(score, 0.0f, 1.0f);
}

} // namespace BeatMate::Services::Suggestions
