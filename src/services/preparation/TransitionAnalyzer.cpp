#include "TransitionAnalyzer.h"
#include "../suggestions/TrackCompatibility.h"
#include <spdlog/spdlog.h>

namespace BeatMate::Services::Preparation {

TransitionQuality TransitionAnalyzer::analyze(const Models::Track& a, const Models::Track& b) {
    auto compat = Suggestions::TrackCompatibility::calculateScore(a, b);
    TransitionQuality q;
    q.bpmScore = compat.bpm;
    q.keyScore = compat.key;
    q.energyScore = compat.energy;
    q.overall = compat.overall;

    if (q.overall >= 0.8f) q.recommendation = "Excellent transition";
    else if (q.overall >= 0.6f) q.recommendation = "Good transition";
    else if (q.overall >= 0.4f) q.recommendation = "Moderate - consider EQ work";
    else q.recommendation = "Difficult transition - use effects or cuts";

    spdlog::debug("TransitionAnalyzer: {} -> {}: {:.1f}% ({})", a.title, b.title, q.overall * 100, q.recommendation);
    return q;
}

} // namespace BeatMate::Services::Preparation
