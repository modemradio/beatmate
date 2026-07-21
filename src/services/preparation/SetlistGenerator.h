#pragma once
#include <vector>
#include "../../models/Track.h"
#include "EnergyProfileGen.h"

namespace BeatMate::Services::Preparation {
class SetlistGenerator {
public:
    SetlistGenerator() = default;
    std::vector<Models::Track> generate(const std::vector<Models::Track>& pool, double durationMinutes, const EnergyCurve& profile);
};
} // namespace BeatMate::Services::Preparation
