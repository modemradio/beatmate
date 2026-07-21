#include "StorylinePlannerService.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace BeatMate::Services::Preparation {

StoryArc StorylinePlannerService::planStoryline(const std::vector<Models::Track>& pool, const StorylineConfig& config) {
    StoryArc arc;
    arc.name = config.arcName.empty() ? "Custom Arc" : config.arcName;
    arc.chapters = generateChapters(config);

    std::vector<bool> used(pool.size(), false);

    for (auto& chapter : arc.chapters) {
        chapter.tracks = selectTracksForChapter(pool, chapter, used);
        for (const auto& t : chapter.tracks) {
            arc.totalDuration += t.duration;
        }
        arc.totalTracks += static_cast<int>(chapter.tracks.size());
    }

    arc.narrativeScore = narrativeCoherence(arc);
    arc.description = "Story arc '" + arc.name + "' with " + std::to_string(arc.chapters.size()) +
                      " chapters, " + std::to_string(arc.totalTracks) + " tracks";

    spdlog::info("StorylinePlannerService: {} - {} chapters, {} tracks, narrative={:.0f}%",
                 arc.name, arc.chapters.size(), arc.totalTracks, arc.narrativeScore * 100.0f);
    return arc;
}

StoryArc StorylinePlannerService::planClassicArc(const std::vector<Models::Track>& pool, double durationMinutes) {
    StorylineConfig config;
    config.arcName = "Classic Arc";
    config.totalDurationMinutes = durationMinutes;
    config.numChapters = 5;
    config.openingEnergy = 3.0f;
    config.climaxEnergy = 9.0f;
    config.resolutionEnergy = 3.0f;
    return planStoryline(pool, config);
}

StoryArc StorylinePlannerService::planDoubleDropArc(const std::vector<Models::Track>& pool, double durationMinutes) {
    StorylineConfig config;
    config.arcName = "Double Drop";
    config.totalDurationMinutes = durationMinutes;
    config.numChapters = 7;
    config.openingEnergy = 4.0f;
    config.climaxEnergy = 9.5f;
    config.resolutionEnergy = 4.0f;
    config.includeBreakdown = true;
    return planStoryline(pool, config);
}

StoryArc StorylinePlannerService::planMarathonArc(const std::vector<Models::Track>& pool, double durationMinutes) {
    StorylineConfig config;
    config.arcName = "Marathon";
    config.totalDurationMinutes = durationMinutes;
    config.numChapters = 10;
    config.openingEnergy = 2.0f;
    config.climaxEnergy = 8.0f;
    config.resolutionEnergy = 3.0f;
    return planStoryline(pool, config);
}

std::vector<StoryChapter> StorylinePlannerService::generateChapters(const StorylineConfig& config) {
    std::vector<StoryChapter> chapters;
    double chapterDuration = config.totalDurationMinutes / config.numChapters;

    for (int i = 0; i < config.numChapters; ++i) {
        StoryChapter chapter;
        float position = static_cast<float>(i) / static_cast<float>(config.numChapters - 1);
        chapter.durationMinutes = chapterDuration;

        if (position < 0.2f) {
            chapter.phase = StoryPhase::Opening;
            chapter.name = "Opening";
            chapter.mood = "atmospheric";
            chapter.targetEnergyStart = config.openingEnergy;
            chapter.targetEnergyEnd = config.openingEnergy + (config.climaxEnergy - config.openingEnergy) * 0.2f;
        } else if (position < 0.4f) {
            chapter.phase = StoryPhase::RisingAction;
            chapter.name = "Building";
            chapter.mood = "building";
            float t = (position - 0.2f) / 0.2f;
            chapter.targetEnergyStart = config.openingEnergy + (config.climaxEnergy - config.openingEnergy) * (0.2f + 0.3f * t);
            chapter.targetEnergyEnd = config.openingEnergy + (config.climaxEnergy - config.openingEnergy) * (0.2f + 0.3f * (t + 0.5f));
        } else if (position < 0.7f) {
            chapter.phase = StoryPhase::Climax;
            chapter.name = "Peak Time";
            chapter.mood = "euphoric";
            chapter.targetEnergyStart = config.climaxEnergy - 1.0f;
            chapter.targetEnergyEnd = config.climaxEnergy;

            if (config.includeBreakdown && i > 0 && position > 0.5f && position < 0.6f) {
                chapter.name = "Breakdown";
                chapter.mood = "emotional";
                chapter.targetEnergyStart = config.climaxEnergy - 3.0f;
                chapter.targetEnergyEnd = config.climaxEnergy;
            }
        } else if (position < 0.85f) {
            chapter.phase = StoryPhase::FallingAction;
            chapter.name = "Wind Down";
            chapter.mood = "groovy";
            float t = (position - 0.7f) / 0.15f;
            chapter.targetEnergyStart = config.climaxEnergy - (config.climaxEnergy - config.resolutionEnergy) * t * 0.5f;
            chapter.targetEnergyEnd = config.climaxEnergy - (config.climaxEnergy - config.resolutionEnergy) * (t * 0.5f + 0.3f);
        } else {
            chapter.phase = StoryPhase::Resolution;
            chapter.name = "Closing";
            chapter.mood = "reflective";
            chapter.targetEnergyStart = config.resolutionEnergy + 1.0f;
            chapter.targetEnergyEnd = config.resolutionEnergy;
        }

        chapters.push_back(chapter);
    }
    return chapters;
}

std::vector<Models::Track> StorylinePlannerService::selectTracksForChapter(
    const std::vector<Models::Track>& pool, const StoryChapter& chapter, std::vector<bool>& used) {

    double targetSeconds = chapter.durationMinutes * 60.0;
    float avgTargetEnergy = (chapter.targetEnergyStart + chapter.targetEnergyEnd) / 2.0f;

    struct Candidate { size_t index; float score; };
    std::vector<Candidate> candidates;

    for (size_t i = 0; i < pool.size(); ++i) {
        if (used[i] || pool[i].duration <= 0) continue;
        float energyDiff = std::abs(pool[i].energy - avgTargetEnergy);
        float score = 1.0f - energyDiff / 10.0f;
        candidates.push_back({i, score});
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.score > b.score; });

    std::vector<Models::Track> selected;
    double totalDuration = 0.0;

    for (const auto& c : candidates) {
        if (totalDuration >= targetSeconds) break;
        selected.push_back(pool[c.index]);
        used[c.index] = true;
        totalDuration += pool[c.index].duration;
    }

    // Sort by energy within chapter (ascending for rising, descending for falling)
    if (chapter.targetEnergyEnd >= chapter.targetEnergyStart) {
        std::sort(selected.begin(), selected.end(), [](const auto& a, const auto& b) { return a.energy < b.energy; });
    } else {
        std::sort(selected.begin(), selected.end(), [](const auto& a, const auto& b) { return a.energy > b.energy; });
    }

    return selected;
}

float StorylinePlannerService::narrativeCoherence(const StoryArc& arc) const {
    if (arc.chapters.empty()) return 0.0f;

    float coherence = 0.0f;
    int checks = 0;

    for (size_t i = 0; i < arc.chapters.size(); ++i) {
        const auto& ch = arc.chapters[i];
        if (ch.tracks.empty()) continue;

        float avgEnergy = 0.0f;
        for (const auto& t : ch.tracks) avgEnergy += t.energy;
        avgEnergy /= static_cast<float>(ch.tracks.size());

        float targetAvg = (ch.targetEnergyStart + ch.targetEnergyEnd) / 2.0f;
        float fit = 1.0f - std::abs(avgEnergy - targetAvg) / 10.0f;
        coherence += std::max(0.0f, fit);
        ++checks;
    }

    return checks > 0 ? coherence / static_cast<float>(checks) : 0.0f;
}

std::string StorylinePlannerService::phaseToString(StoryPhase phase) const {
    switch (phase) {
        case StoryPhase::Opening: return "Opening";
        case StoryPhase::RisingAction: return "Rising Action";
        case StoryPhase::Climax: return "Climax";
        case StoryPhase::FallingAction: return "Falling Action";
        case StoryPhase::Resolution: return "Resolution";
    }
    return "Unknown";
}

} // namespace BeatMate::Services::Preparation
