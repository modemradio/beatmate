#include "SmartTransitionGen.h"
#include <spdlog/spdlog.h>
#include <cmath>

namespace BeatMate::Services::AI {

TransitionSuggestion SmartTransitionGen::suggest(const Models::Track& trackA, const Models::Track& trackB) {
    TransitionSuggestion suggestion;
    suggestion.transitionType = determineTransitionType(trackA, trackB);
    suggestion.mixOutPoint = calculateMixOutPoint(trackA);
    suggestion.mixInPoint = calculateMixInPoint(trackB);

    // Calculate transition length based on BPM
    double avgBpm = (trackA.bpm + trackB.bpm) / 2.0;
    double barsPerSecond = avgBpm / (60.0 * 4.0);
    suggestion.transitionLength = 16.0; // 16 bars default

    if (std::abs(trackA.bpm - trackB.bpm) > trackA.bpm * 0.05) {
        suggestion.transitionLength = 32.0; // Longer transition for bigger BPM diff
    }

    suggestion.confidence = 0.8f;
    suggestion.description = suggestion.transitionType + " over " +
                             std::to_string(static_cast<int>(suggestion.transitionLength)) + " bars";

    spdlog::debug("SmartTransitionGen: {} -> {}: {}", trackA.title, trackB.title, suggestion.description);
    return suggestion;
}

std::string SmartTransitionGen::determineTransitionType(const Models::Track& a, const Models::Track& b) const {
    double bpmDiff = std::abs(a.bpm - b.bpm);
    float energyDiff = std::abs(a.energy - b.energy);

    if (bpmDiff < 2.0 && energyDiff < 2.0) return "blend";
    if (bpmDiff > a.bpm * 0.1) return "cut";
    if (energyDiff > 4.0 && b.energy > a.energy) return "filter";
    if (energyDiff > 4.0 && b.energy < a.energy) return "echo";
    return "blend";
}

double SmartTransitionGen::calculateMixOutPoint(const Models::Track& track) const {
    return track.duration > 30.0 ? track.duration - 32.0 : track.duration - 16.0;
}

double SmartTransitionGen::calculateMixInPoint(const Models::Track& track) const {
    return track.duration > 30.0 ? 16.0 : 8.0;
}

} // namespace BeatMate::Services::AI
