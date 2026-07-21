#include "QuickSetPlannerService.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <random>

namespace BeatMate::Services::Preparation {

QuickPlanResult QuickSetPlannerService::planOneClick(const std::vector<Models::Track>& pool, const QuickPlanConfig& config) {
    switch (config.mode) {
        case QuickPlanMode::AutoBpm: return planAutoBpm(pool, config.durationMinutes);
        case QuickPlanMode::AutoEnergy: return planAutoEnergy(pool, config.durationMinutes, config.startEnergy, config.peakEnergy, config.endEnergy);
        case QuickPlanMode::AutoHarmonic: return planAutoHarmonic(pool, config.durationMinutes);
        case QuickPlanMode::PartyMix: return planPartyMix(pool, config.durationMinutes);
        case QuickPlanMode::ChillSession: return planAutoEnergy(pool, config.durationMinutes, 2.0f, 5.0f, 2.0f);
        case QuickPlanMode::Progressive: return planAutoEnergy(pool, config.durationMinutes, 3.0f, 9.0f, 5.0f);
    }
    return planAutoBpm(pool, config.durationMinutes);
}

QuickPlanResult QuickSetPlannerService::planAutoBpm(const std::vector<Models::Track>& pool, double durationMinutes) {
    auto selected = selectTracks(pool, durationMinutes, {});
    std::sort(selected.begin(), selected.end(), [](const auto& a, const auto& b) { return a.bpm < b.bpm; });

    PlannerConfig pc;
    pc.bpmWeight = 0.5f;
    pc.keyWeight = 0.3f;
    pc.energyWeight = 0.1f;
    pc.genreWeight = 0.1f;
    auto planResult = planner_.planNearestNeighbor(selected, pc);
    return buildResult(planResult.orderedTracks, "Auto BPM");
}

QuickPlanResult QuickSetPlannerService::planAutoEnergy(
    const std::vector<Models::Track>& pool, double durationMinutes, float startE, float peakE, float endE) {

    auto selected = selectTracks(pool, durationMinutes, {});
    if (selected.size() < 3) return buildResult(selected, "Auto Energy");

    size_t warmup = selected.size() / 3;
    size_t peak = warmup + selected.size() / 3;

    std::sort(selected.begin(), selected.end(), [](const auto& a, const auto& b) { return a.energy < b.energy; });

    std::vector<Models::Track> warmupTracks, peakTracks, cooldownTracks;
    for (const auto& t : selected) {
        float distStart = std::abs(t.energy - startE);
        float distPeak = std::abs(t.energy - peakE);
        float distEnd = std::abs(t.energy - endE);

        if (distPeak <= distStart && distPeak <= distEnd && peakTracks.size() < selected.size() / 3 + 1)
            peakTracks.push_back(t);
        else if (distStart <= distEnd && warmupTracks.size() < selected.size() / 3 + 1)
            warmupTracks.push_back(t);
        else
            cooldownTracks.push_back(t);
    }

    std::sort(warmupTracks.begin(), warmupTracks.end(), [](const auto& a, const auto& b) { return a.energy < b.energy; });
    std::sort(peakTracks.begin(), peakTracks.end(), [](const auto& a, const auto& b) { return a.energy < b.energy; });
    std::sort(cooldownTracks.begin(), cooldownTracks.end(), [](const auto& a, const auto& b) { return a.energy > b.energy; });

    std::vector<Models::Track> ordered;
    ordered.insert(ordered.end(), warmupTracks.begin(), warmupTracks.end());
    ordered.insert(ordered.end(), peakTracks.begin(), peakTracks.end());
    ordered.insert(ordered.end(), cooldownTracks.begin(), cooldownTracks.end());

    return buildResult(ordered, "Auto Energy");
}

QuickPlanResult QuickSetPlannerService::planAutoHarmonic(const std::vector<Models::Track>& pool, double durationMinutes) {
    auto selected = selectTracks(pool, durationMinutes, {});

    PlannerConfig pc;
    pc.bpmWeight = 0.1f;
    pc.keyWeight = 0.6f;
    pc.energyWeight = 0.2f;
    pc.genreWeight = 0.1f;
    auto planResult = planner_.planNearestNeighbor(selected, pc);
    return buildResult(planResult.orderedTracks, "Auto Harmonic");
}

QuickPlanResult QuickSetPlannerService::planPartyMix(const std::vector<Models::Track>& pool, double durationMinutes) {
    auto selected = selectTracks(pool, durationMinutes, {});

    std::sort(selected.begin(), selected.end(), [](const auto& a, const auto& b) { return a.energy > b.energy; });

    size_t keep = std::max(size_t(3), selected.size() * 7 / 10);
    if (selected.size() > keep) selected.resize(keep);

    PlannerConfig pc;
    pc.bpmWeight = 0.3f;
    pc.keyWeight = 0.3f;
    pc.energyWeight = 0.2f;
    pc.genreWeight = 0.2f;
    auto planResult = planner_.planNearestNeighbor(selected, pc);
    return buildResult(planResult.orderedTracks, "Party Mix");
}

std::vector<Models::Track> QuickSetPlannerService::selectTracks(
    const std::vector<Models::Track>& pool, double durationMinutes, const QuickPlanConfig& config) {

    std::vector<Models::Track> filtered;
    for (const auto& t : pool) {
        if (t.duration <= 0) continue;
        if (!config.preferredGenre.empty() && t.genre != config.preferredGenre) continue;
        filtered.push_back(t);
    }

    double targetSeconds = durationMinutes * 60.0;
    std::vector<Models::Track> selected;
    double totalDuration = 0.0;

    for (const auto& t : filtered) {
        if (totalDuration >= targetSeconds) break;
        if (config.maxTracks > 0 && static_cast<int>(selected.size()) >= config.maxTracks) break;
        selected.push_back(t);
        totalDuration += t.duration;
    }
    return selected;
}

QuickPlanResult QuickSetPlannerService::buildResult(const std::vector<Models::Track>& tracks, const std::string& modeName) {
    QuickPlanResult result;
    result.tracks = tracks;
    result.modeName = modeName;
    result.trackCount = static_cast<int>(tracks.size());
    result.totalDuration = 0.0;
    for (const auto& t : tracks) result.totalDuration += t.duration;
    result.validation = validator_.validate(tracks);
    result.qualityScore = result.validation.overallScore;
    spdlog::info("QuickSetPlannerService: {} - {} tracks, {:.1f} min, quality={:.0f}%",
                 modeName, result.trackCount, result.totalDuration / 60.0, result.qualityScore * 100.0f);
    return result;
}

} // namespace BeatMate::Services::Preparation
