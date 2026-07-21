#pragma once
#include <string>
#include <vector>
#include "../../models/Track.h"

namespace BeatMate::Services::Preparation {

enum class StoryPhase { Opening, RisingAction, Climax, FallingAction, Resolution };

struct StoryChapter {
    std::string name;
    StoryPhase phase = StoryPhase::Opening;
    float targetEnergyStart = 0.0f;
    float targetEnergyEnd = 0.0f;
    double durationMinutes = 10.0;
    std::string mood;
    std::vector<Models::Track> tracks;
};

struct StoryArc {
    std::string name;
    std::vector<StoryChapter> chapters;
    double totalDuration = 0.0;
    int totalTracks = 0;
    float narrativeScore = 0.0f;
    std::string description;
};

struct StorylineConfig {
    std::string arcName;
    double totalDurationMinutes = 60.0;
    int numChapters = 5;
    float openingEnergy = 3.0f;
    float climaxEnergy = 9.0f;
    float resolutionEnergy = 3.0f;
    bool includeBreakdown = true;
    std::string themeGenre;
};

class StorylinePlannerService {
public:
    StorylinePlannerService() = default;

    StoryArc planStoryline(const std::vector<Models::Track>& pool, const StorylineConfig& config);
    StoryArc planClassicArc(const std::vector<Models::Track>& pool, double durationMinutes);
    StoryArc planDoubleDropArc(const std::vector<Models::Track>& pool, double durationMinutes);
    StoryArc planMarathonArc(const std::vector<Models::Track>& pool, double durationMinutes);
    std::vector<StoryChapter> generateChapters(const StorylineConfig& config);

private:
    std::vector<Models::Track> selectTracksForChapter(const std::vector<Models::Track>& pool,
                                                       const StoryChapter& chapter,
                                                       std::vector<bool>& used);
    float narrativeCoherence(const StoryArc& arc) const;
    std::string phaseToString(StoryPhase phase) const;
};

} // namespace BeatMate::Services::Preparation
