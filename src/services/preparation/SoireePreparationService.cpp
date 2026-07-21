#include "SoireePreparationService.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace BeatMate::Services::Preparation {

SoireePlan SoireePreparationService::prepareSoiree(const std::vector<Models::Track>& pool, const SoireeProfile& profile) {
    SoireePlan plan;
    plan.profile = profile;
    plan.description = profile.name;

    auto phases = getPhasesForType(profile.type);
    double totalSeconds = profile.durationHours * 3600.0;
    std::vector<bool> used(pool.size(), false);

    for (const auto& phase : phases) {
        double phaseDuration = totalSeconds * phase.durationRatio;
        plan.phases.push_back(phase.name);

        auto tracks = selectTracksForPhase(pool, phase, phaseDuration, used);
        plan.playlist.insert(plan.playlist.end(), tracks.begin(), tracks.end());
    }

    plan.trackCount = static_cast<int>(plan.playlist.size());
    plan.totalDuration = 0.0;
    for (const auto& t : plan.playlist) {
        plan.totalDuration += t.duration;
        plan.genreDistribution[t.genre.empty() ? "Unknown" : t.genre]++;
    }

    plan.qualityScore = evaluatePlan(plan);

    spdlog::info("SoireePreparationService: '{}' - {} tracks, {:.1f}h, quality={:.0f}%",
                 profile.name, plan.trackCount, plan.totalDuration / 3600.0, plan.qualityScore * 100.0f);
    return plan;
}

SoireePlan SoireePreparationService::prepareClubNight(const std::vector<Models::Track>& pool, double hours, const std::string& genre) {
    SoireeProfile profile = defaultProfile(SoireeType::Club);
    profile.durationHours = hours;
    profile.name = "Club Night - " + genre;
    profile.genres = {genre};
    return prepareSoiree(pool, profile);
}

SoireePlan SoireePreparationService::prepareLounge(const std::vector<Models::Track>& pool, double hours) {
    SoireeProfile profile = defaultProfile(SoireeType::Lounge);
    profile.durationHours = hours;
    return prepareSoiree(pool, profile);
}

SoireePlan SoireePreparationService::prepareFestivalSet(const std::vector<Models::Track>& pool, double hours) {
    SoireeProfile profile = defaultProfile(SoireeType::Festival);
    profile.durationHours = hours;
    return prepareSoiree(pool, profile);
}

SoireePlan SoireePreparationService::prepareWedding(const std::vector<Models::Track>& pool, double hours) {
    SoireeProfile profile = defaultProfile(SoireeType::Wedding);
    profile.durationHours = hours;
    return prepareSoiree(pool, profile);
}

SoireeProfile SoireePreparationService::defaultProfile(SoireeType type) {
    SoireeProfile p;
    p.type = type;
    switch (type) {
        case SoireeType::Club:
            p.name = "Club Night"; p.peakEnergyTarget = 8.5f; p.durationHours = 5.0;
            p.genres = {"House", "Tech House", "Techno"}; p.ambiance = "energetic"; break;
        case SoireeType::Lounge:
            p.name = "Lounge Session"; p.peakEnergyTarget = 5.0f; p.durationHours = 4.0;
            p.genres = {"Deep House", "Chill", "Jazz"}; p.ambiance = "intimate"; break;
        case SoireeType::Festival:
            p.name = "Festival Set"; p.peakEnergyTarget = 9.5f; p.durationHours = 1.5;
            p.genres = {"EDM", "House", "Trance"}; p.ambiance = "wild"; break;
        case SoireeType::PrivateParty:
            p.name = "Private Party"; p.peakEnergyTarget = 7.0f; p.durationHours = 4.0;
            p.genres = {"Pop", "Hip Hop", "R&B"}; p.ambiance = "energetic"; break;
        case SoireeType::Wedding:
            p.name = "Wedding"; p.peakEnergyTarget = 7.0f; p.durationHours = 5.0;
            p.genres = {"Pop", "Soul", "Disco"}; p.ambiance = "elegant"; break;
        case SoireeType::Corporate:
            p.name = "Corporate Event"; p.peakEnergyTarget = 5.0f; p.durationHours = 3.0;
            p.genres = {"Lounge", "Jazz", "Pop"}; p.ambiance = "chill"; break;
        case SoireeType::AfterParty:
            p.name = "After Party"; p.peakEnergyTarget = 7.0f; p.durationHours = 3.0;
            p.genres = {"Deep House", "Minimal", "Techno"}; p.ambiance = "intimate"; break;
    }
    return p;
}

std::vector<SoireePreparationService::PhaseConfig> SoireePreparationService::getPhasesForType(SoireeType type) const {
    switch (type) {
        case SoireeType::Club:
            return {
                {"Warm-up", 3.0f, 5.0f, 0.2f},
                {"Building", 5.0f, 7.0f, 0.2f},
                {"Peak Time", 7.0f, 9.0f, 0.35f},
                {"Cool Down", 6.0f, 4.0f, 0.25f}
            };
        case SoireeType::Lounge:
            return {
                {"Ambient", 2.0f, 3.0f, 0.3f},
                {"Groove", 3.0f, 5.0f, 0.4f},
                {"Wind Down", 4.0f, 2.0f, 0.3f}
            };
        case SoireeType::Festival:
            return {
                {"Intro", 5.0f, 7.0f, 0.15f},
                {"Build", 7.0f, 8.5f, 0.25f},
                {"Peak", 8.5f, 9.5f, 0.4f},
                {"Finale", 9.0f, 7.0f, 0.2f}
            };
        case SoireeType::Wedding:
            return {
                {"Cocktail", 2.0f, 3.0f, 0.2f},
                {"Dinner", 3.0f, 4.0f, 0.25f},
                {"Party", 5.0f, 8.0f, 0.35f},
                {"Last Dances", 6.0f, 3.0f, 0.2f}
            };
        default:
            return {
                {"Opening", 3.0f, 5.0f, 0.25f},
                {"Main", 5.0f, 7.0f, 0.5f},
                {"Closing", 5.0f, 3.0f, 0.25f}
            };
    }
}

std::vector<Models::Track> SoireePreparationService::selectTracksForPhase(
    const std::vector<Models::Track>& pool, const PhaseConfig& phase, double phaseDuration,
    std::vector<bool>& used) {

    float avgEnergy = (phase.energyStart + phase.energyEnd) / 2.0f;

    struct Candidate { size_t index; float score; };
    std::vector<Candidate> candidates;

    for (size_t i = 0; i < pool.size(); ++i) {
        if (used[i] || pool[i].duration <= 0) continue;
        float energyDiff = std::abs(pool[i].energy - avgEnergy);
        float score = 1.0f - energyDiff / 10.0f;
        candidates.push_back({i, score});
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.score > b.score; });

    std::vector<Models::Track> selected;
    double totalDuration = 0.0;

    for (const auto& c : candidates) {
        if (totalDuration >= phaseDuration) break;
        selected.push_back(pool[c.index]);
        used[c.index] = true;
        totalDuration += pool[c.index].duration;
    }

    if (phase.energyEnd >= phase.energyStart) {
        std::sort(selected.begin(), selected.end(), [](const auto& a, const auto& b) { return a.energy < b.energy; });
    } else {
        std::sort(selected.begin(), selected.end(), [](const auto& a, const auto& b) { return a.energy > b.energy; });
    }

    return selected;
}

float SoireePreparationService::evaluatePlan(const SoireePlan& plan) const {
    if (plan.playlist.empty()) return 0.0f;

    float score = 0.0f;
    int checks = 0;

    double targetDuration = plan.profile.durationHours * 3600.0;
    float coverage = static_cast<float>(std::min(1.0, plan.totalDuration / targetDuration));
    score += coverage;
    ++checks;

    if (plan.playlist.size() > 1) {
        float bpmSmooth = 0.0f;
        for (size_t i = 1; i < plan.playlist.size(); ++i) {
            double diff = std::abs(plan.playlist[i].bpm - plan.playlist[i - 1].bpm);
            bpmSmooth += (diff <= 6.0) ? 1.0f : 0.0f;
        }
        bpmSmooth /= static_cast<float>(plan.playlist.size() - 1);
        score += bpmSmooth;
        ++checks;
    }

    float energyFlow = 0.0f;
    for (size_t i = 1; i < plan.playlist.size(); ++i) {
        float diff = std::abs(plan.playlist[i].energy - plan.playlist[i - 1].energy);
        energyFlow += (diff <= 2.0f) ? 1.0f : 0.0f;
    }
    if (plan.playlist.size() > 1) {
        energyFlow /= static_cast<float>(plan.playlist.size() - 1);
        score += energyFlow;
        ++checks;
    }

    return checks > 0 ? score / static_cast<float>(checks) : 0.0f;
}

} // namespace BeatMate::Services::Preparation
