#include "SetlistGenerator.h"
#include "../suggestions/TrackCompatibility.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace BeatMate::Services::Preparation {

std::vector<Models::Track> SetlistGenerator::generate(const std::vector<Models::Track>& pool,
    double durationMinutes, const EnergyCurve& profile) {
    if (pool.empty() || profile.values.empty()) return {};

    std::vector<Models::Track> result;
    double totalDuration = 0.0;
    double targetDuration = durationMinutes * 60.0;
    std::vector<bool> used(pool.size(), false);

    double avgTrackDuration = 0;
    for (const auto& t : pool) avgTrackDuration += t.duration;
    avgTrackDuration /= pool.size();
    int numSlots = static_cast<int>(targetDuration / avgTrackDuration);

    for (int slot = 0; slot < numSlots && totalDuration < targetDuration; ++slot) {
        float t = static_cast<float>(slot) / numSlots;
        int profileIdx = static_cast<int>(t * profile.values.size());
        profileIdx = std::clamp(profileIdx, 0, static_cast<int>(profile.values.size()) - 1);
        float targetEnergy = profile.values[static_cast<size_t>(profileIdx)];

        float bestScore = -1.0f;
        size_t bestIdx = 0;

        for (size_t i = 0; i < pool.size(); ++i) {
            if (used[i]) continue;
            float energyDiff = std::abs(pool[i].energy - targetEnergy);
            float score = 1.0f - energyDiff / 10.0f;

            if (!result.empty()) {
                auto compat = Suggestions::TrackCompatibility::calculateScore(result.back(), pool[i]);
                score = score * 0.4f + compat.overall * 0.6f;
            }

            if (score > bestScore) { bestScore = score; bestIdx = i; }
        }

        used[bestIdx] = true;
        result.push_back(pool[bestIdx]);
        totalDuration += pool[bestIdx].duration;
    }

    spdlog::info("SetlistGenerator: Generated setlist of {} tracks, {:.1f} minutes", result.size(), totalDuration / 60.0);
    return result;
}

}
