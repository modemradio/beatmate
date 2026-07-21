#pragma once
#include <string>
#include <vector>
#include "../../models/Track.h"
#include "SetPlannerEngine.h"
#include "SetValidationService.h"

namespace BeatMate::Services::Preparation {

enum class QuickPlanMode { AutoBpm, AutoEnergy, AutoHarmonic, PartyMix, ChillSession, Progressive };

struct QuickPlanConfig {
    QuickPlanMode mode = QuickPlanMode::AutoBpm;
    double durationMinutes = 60.0;
    int maxTracks = 0;
    std::string preferredGenre;
    float startEnergy = 3.0f;
    float peakEnergy = 8.0f;
    float endEnergy = 4.0f;
};

struct QuickPlanResult {
    std::vector<Models::Track> tracks;
    double totalDuration = 0.0;
    float qualityScore = 0.0f;
    std::string modeName;
    int trackCount = 0;
    ValidationReport validation;
};

class QuickSetPlannerService {
public:
    QuickSetPlannerService() = default;

    QuickPlanResult planOneClick(const std::vector<Models::Track>& pool, const QuickPlanConfig& config);
    QuickPlanResult planAutoBpm(const std::vector<Models::Track>& pool, double durationMinutes);
    QuickPlanResult planAutoEnergy(const std::vector<Models::Track>& pool, double durationMinutes, float startE, float peakE, float endE);
    QuickPlanResult planAutoHarmonic(const std::vector<Models::Track>& pool, double durationMinutes);
    QuickPlanResult planPartyMix(const std::vector<Models::Track>& pool, double durationMinutes);

private:
    std::vector<Models::Track> selectTracks(const std::vector<Models::Track>& pool, double durationMinutes, const QuickPlanConfig& config);
    void optimizeOrder(std::vector<Models::Track>& tracks, const QuickPlanConfig& config);
    QuickPlanResult buildResult(const std::vector<Models::Track>& tracks, const std::string& modeName);

    SetPlannerEngine planner_;
    SetValidationService validator_;
};

}
