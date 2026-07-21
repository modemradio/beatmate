#include "BeatSync.h"
#include <cmath>

namespace BeatMate::Core {

SyncResult BeatSync::sync(double bpmA, double bpmB, double phaseA, double phaseB) {
    SyncResult result;
    result.tempoAdjustment = bpmA / bpmB;
    result.phaseOffset = phaseA - phaseB;
    result.needsAdjustment = std::fabs(result.tempoAdjustment - 1.0) > 0.001 ||
                             std::fabs(result.phaseOffset) > 0.01;
    return result;
}

double BeatSync::calculateTempoRatio(double bpmMaster, double bpmSlave) {
    if (bpmSlave <= 0) return 1.0;
    return bpmMaster / bpmSlave;
}

double BeatSync::calculatePhaseOffset(double posA, double posB, double bpm) {
    double beatDuration = 60.0 / bpm;
    double phaseA = std::fmod(posA, beatDuration) / beatDuration;
    double phaseB = std::fmod(posB, beatDuration) / beatDuration;
    double offset = phaseA - phaseB;
    if (offset > 0.5) offset -= 1.0;
    if (offset < -0.5) offset += 1.0;
    return offset * beatDuration;
}

} // namespace BeatMate::Core
