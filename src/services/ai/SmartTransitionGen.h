#pragma once
#include <string>
#include "../../models/Track.h"

namespace BeatMate::Services::AI {

struct TransitionSuggestion {
    double mixInPoint = 0.0;    // seconds into track B
    double mixOutPoint = 0.0;   // seconds before end of track A
    double transitionLength = 16.0; // bars
    std::string transitionType; // "blend", "cut", "echo", "filter", "spinback"
    float confidence = 0.0f;
    std::string description;
};

class SmartTransitionGen {
public:
    SmartTransitionGen() = default;
    ~SmartTransitionGen() = default;
    TransitionSuggestion suggest(const Models::Track& trackA, const Models::Track& trackB);
private:
    std::string determineTransitionType(const Models::Track& a, const Models::Track& b) const;
    double calculateMixOutPoint(const Models::Track& track) const;
    double calculateMixInPoint(const Models::Track& track) const;
};

} // namespace BeatMate::Services::AI
