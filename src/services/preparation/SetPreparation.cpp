#include "SetPreparation.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <numeric>

namespace BeatMate::Services::Preparation {

SetPlan SetPreparation::prepareSet(const std::vector<Models::Track>& tracks, const std::string& eventName) {
    SetPlan plan;
    plan.name = eventName.empty() ? "New Set" : eventName;
    plan.tracks = tracks;
    calculateTotalDuration(plan);
    optimizeEnergyCurve(plan);
    spdlog::info("SetPreparation: Prepared set '{}' with {} tracks, {:.1f} min", plan.name, plan.tracks.size(), plan.totalDuration / 60.0);
    return plan;
}

void SetPreparation::optimizeEnergyCurve(SetPlan& plan) {
    if (plan.tracks.size() <= 3) return;

    size_t warmup = plan.tracks.size() / 3;
    size_t peak = warmup + plan.tracks.size() / 3;

    auto warmupTracks = std::vector<Models::Track>(plan.tracks.begin(), plan.tracks.begin() + warmup);
    auto peakTracks = std::vector<Models::Track>(plan.tracks.begin() + warmup, plan.tracks.begin() + peak);
    auto cooldownTracks = std::vector<Models::Track>(plan.tracks.begin() + peak, plan.tracks.end());

    std::sort(warmupTracks.begin(), warmupTracks.end(), [](const auto& a, const auto& b) { return a.energy < b.energy; });
    std::sort(peakTracks.begin(), peakTracks.end(), [](const auto& a, const auto& b) { return a.energy > b.energy; });
    std::sort(cooldownTracks.begin(), cooldownTracks.end(), [](const auto& a, const auto& b) { return a.energy > b.energy; });

    plan.tracks.clear();
    plan.tracks.insert(plan.tracks.end(), warmupTracks.begin(), warmupTracks.end());
    plan.tracks.insert(plan.tracks.end(), peakTracks.begin(), peakTracks.end());
    plan.tracks.insert(plan.tracks.end(), cooldownTracks.begin(), cooldownTracks.end());
}

void SetPreparation::calculateTotalDuration(SetPlan& plan) {
    plan.totalDuration = 0;
    for (const auto& t : plan.tracks) plan.totalDuration += t.duration;
}

}
