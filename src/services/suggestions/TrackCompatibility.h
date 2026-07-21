#pragma once
#include <string>
#include "../../models/Track.h"

namespace BeatMate::Services::Suggestions {

struct CompatibilityScore {
    float overall = 0.0f;
    float bpm = 0.0f;
    float key = 0.0f;
    float energy = 0.0f;
    float genre = 0.0f;
};

class TrackCompatibility {
public:
    TrackCompatibility() = default;
    static CompatibilityScore calculateScore(const Models::Track& a, const Models::Track& b);
    static float bpmCompatibility(double bpm1, double bpm2);
    static float keyCompatibility(const std::string& key1, const std::string& key2);
    static float energyCompatibility(float e1, float e2);
    static float genreCompatibility(const std::string& g1, const std::string& g2);
};

} // namespace BeatMate::Services::Suggestions
