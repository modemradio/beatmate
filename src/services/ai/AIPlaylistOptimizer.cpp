#include "AIPlaylistOptimizer.h"
#include "../suggestions/TrackCompatibility.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace BeatMate::Services::AI {

std::vector<Models::Track> AIPlaylistOptimizer::optimize(const std::vector<Models::Track>& playlist,
                                                          const OptimizationCriteria& criteria) {
    if (playlist.size() <= 2) return playlist;

    // Nearest-neighbor heuristic for TSP-like ordering
    std::vector<Models::Track> result;
    std::vector<bool> used(playlist.size(), false);

    result.push_back(playlist[0]);
    used[0] = true;

    for (size_t step = 1; step < playlist.size(); ++step) {
        float bestCost = std::numeric_limits<float>::max();
        size_t bestIdx = 0;

        for (size_t i = 0; i < playlist.size(); ++i) {
            if (used[i]) continue;
            float cost = transitionCost(result.back(), playlist[i], criteria);
            if (cost < bestCost) {
                bestCost = cost;
                bestIdx = i;
            }
        }

        used[bestIdx] = true;
        result.push_back(playlist[bestIdx]);
    }

    spdlog::info("AIPlaylistOptimizer: Optimized playlist of {} tracks", result.size());
    return result;
}

float AIPlaylistOptimizer::transitionCost(const Models::Track& a, const Models::Track& b,
                                           const OptimizationCriteria& c) const {
    auto score = Suggestions::TrackCompatibility::calculateScore(a, b);
    return 1.0f - score.overall; // Lower cost = better transition
}

} // namespace BeatMate::Services::AI
