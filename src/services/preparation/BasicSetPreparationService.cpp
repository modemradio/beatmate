#include "BasicSetPreparationService.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace BeatMate::Services::Preparation {

QuickSetResult BasicSetPreparationService::prepareQuickSet(
    const std::vector<Models::Track>& pool, const QuickSetConfig& config) {

    auto filtered = filterTracks(pool, config);
    sortTracks(filtered, config);
    auto selected = selectForDuration(filtered, config.targetDurationMinutes * 60.0);
    auto result = buildResult("Quick Set", selected);
    spdlog::info("BasicSetPreparationService: Quick set prepared - {} tracks, {:.1f} min",
                 result.trackCount, result.totalDuration / 60.0);
    return result;
}

QuickSetResult BasicSetPreparationService::prepareByGenre(
    const std::vector<Models::Track>& pool, const std::string& genre, double durationMinutes) {

    QuickSetConfig config;
    config.genre = genre;
    config.targetDurationMinutes = durationMinutes;
    auto filtered = filterTracks(pool, config);
    sortTracks(filtered, config);
    auto selected = selectForDuration(filtered, durationMinutes * 60.0);
    auto result = buildResult("Genre Set: " + genre, selected);
    spdlog::info("BasicSetPreparationService: Genre set '{}' - {} tracks", genre, result.trackCount);
    return result;
}

QuickSetResult BasicSetPreparationService::prepareByBpmRange(
    const std::vector<Models::Track>& pool, double bpmMin, double bpmMax, double durationMinutes) {

    QuickSetConfig config;
    config.bpmMin = bpmMin;
    config.bpmMax = bpmMax;
    config.targetDurationMinutes = durationMinutes;
    auto filtered = filterTracks(pool, config);
    sortTracks(filtered, config);
    auto selected = selectForDuration(filtered, durationMinutes * 60.0);
    auto result = buildResult("BPM Set: " + std::to_string(static_cast<int>(bpmMin)) + "-" +
                              std::to_string(static_cast<int>(bpmMax)), selected);
    spdlog::info("BasicSetPreparationService: BPM set [{:.0f}-{:.0f}] - {} tracks", bpmMin, bpmMax, result.trackCount);
    return result;
}

QuickSetResult BasicSetPreparationService::prepareByEnergyRange(
    const std::vector<Models::Track>& pool, float energyMin, float energyMax, double durationMinutes) {

    QuickSetConfig config;
    config.energyMin = energyMin;
    config.energyMax = energyMax;
    config.targetDurationMinutes = durationMinutes;
    config.sortByEnergy = true;
    config.sortByBpm = false;
    auto filtered = filterTracks(pool, config);
    sortTracks(filtered, config);
    auto selected = selectForDuration(filtered, durationMinutes * 60.0);
    auto result = buildResult("Energy Set", selected);
    spdlog::info("BasicSetPreparationService: Energy set [{:.1f}-{:.1f}] - {} tracks", energyMin, energyMax, result.trackCount);
    return result;
}

std::vector<Models::Track> BasicSetPreparationService::filterTracks(
    const std::vector<Models::Track>& pool, const QuickSetConfig& config) {

    std::vector<Models::Track> filtered;
    for (const auto& track : pool) {
        if (config.bpmMin > 0 && track.bpm < config.bpmMin) continue;
        if (config.bpmMax > 0 && track.bpm > config.bpmMax) continue;
        if (track.energy < config.energyMin || track.energy > config.energyMax) continue;
        if (!config.genre.empty() && track.genre != config.genre) continue;
        filtered.push_back(track);
    }
    return filtered;
}

void BasicSetPreparationService::sortTracks(std::vector<Models::Track>& tracks, const QuickSetConfig& config) {
    if (config.sortByBpm) {
        std::sort(tracks.begin(), tracks.end(),
                  [](const auto& a, const auto& b) { return a.bpm < b.bpm; });
    } else if (config.sortByEnergy) {
        std::sort(tracks.begin(), tracks.end(),
                  [](const auto& a, const auto& b) { return a.energy < b.energy; });
    }
}

std::vector<Models::Track> BasicSetPreparationService::selectForDuration(
    const std::vector<Models::Track>& tracks, double targetSeconds) {

    std::vector<Models::Track> selected;
    double totalDuration = 0.0;
    for (const auto& track : tracks) {
        if (totalDuration + track.duration > targetSeconds * 1.1) break;
        selected.push_back(track);
        totalDuration += track.duration;
        if (totalDuration >= targetSeconds) break;
    }
    return selected;
}

QuickSetResult BasicSetPreparationService::buildResult(
    const std::string& name, const std::vector<Models::Track>& tracks) {

    QuickSetResult result;
    result.name = name;
    result.tracks = tracks;
    result.trackCount = static_cast<int>(tracks.size());
    result.totalDuration = 0.0;
    double bpmSum = 0.0;
    float energySum = 0.0f;
    int bpmCount = 0;

    for (const auto& t : tracks) {
        result.totalDuration += t.duration;
        if (t.bpm > 0) { bpmSum += t.bpm; ++bpmCount; }
        energySum += t.energy;
    }

    result.avgBpm = bpmCount > 0 ? bpmSum / bpmCount : 0.0;
    result.avgEnergy = !tracks.empty() ? energySum / static_cast<float>(tracks.size()) : 0.0f;
    return result;
}

} // namespace BeatMate::Services::Preparation
