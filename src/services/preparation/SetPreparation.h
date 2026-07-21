#pragma once
#include <string>
#include <vector>
#include "../../models/Track.h"

namespace BeatMate::Services::Preparation {

struct SetPlan { std::string name; std::vector<Models::Track> tracks; double totalDuration = 0.0; std::string notes; };

class SetPreparation {
public:
    SetPreparation() = default;
    SetPlan prepareSet(const std::vector<Models::Track>& tracks, const std::string& eventName = "");
    void optimizeEnergyCurve(SetPlan& plan);
private:
    void calculateTotalDuration(SetPlan& plan);
};

} // namespace BeatMate::Services::Preparation
