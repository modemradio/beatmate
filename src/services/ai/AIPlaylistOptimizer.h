#pragma once
#include <string>
#include <vector>
#include "../../models/Track.h"

namespace BeatMate::Services::AI {

struct OptimizationCriteria { bool optimizeKey = true; bool optimizeBpm = true; bool optimizeEnergy = true; float energyWeight = 0.3f; };

class AIPlaylistOptimizer {
public:
    AIPlaylistOptimizer() = default;
    std::vector<Models::Track> optimize(const std::vector<Models::Track>& playlist, const OptimizationCriteria& criteria = {});
private:
    float transitionCost(const Models::Track& a, const Models::Track& b, const OptimizationCriteria& c) const;
};

} // namespace BeatMate::Services::AI
