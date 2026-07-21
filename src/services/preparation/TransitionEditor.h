#pragma once

#include <cstdint>
#include <string>

#include "../../models/Track.h"

namespace BeatMate::Services::Preparation {

struct TransitionPlan {
    int64_t trackAId = 0;
    int64_t trackBId = 0;
    double mixOutStart = 0.0;     // seconds in track A
    double mixInStart  = 0.0;     // seconds in track B
    double overlapSec  = 16.0;    // length of overlap
    float  crossfadeCurve = 0.5f; // 0..1 power
    std::string notes;
};

class TransitionEditor {
public:
    // Uses cue markers if available, otherwise falls back to durations
    TransitionPlan suggestDefault(const Models::Track& a, const Models::Track& b);
};

} // namespace BeatMate::Services::Preparation
