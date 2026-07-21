#pragma once
#include <string>
#include "../../models/Track.h"
namespace BeatMate::Services::Preparation {
struct TransitionQuality { float overall = 0.0f; float bpmScore = 0.0f; float keyScore = 0.0f; float energyScore = 0.0f; std::string recommendation; };
class TransitionAnalyzer {
public:
    TransitionAnalyzer() = default;
    TransitionQuality analyze(const Models::Track& a, const Models::Track& b);
};
} // namespace BeatMate::Services::Preparation
