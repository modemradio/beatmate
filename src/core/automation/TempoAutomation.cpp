#include "TempoAutomation.h"
#include <cmath>

namespace BeatMate::Core {

double TempoAutomation::getPositionInBeats(double timeSec) const {
    double step = 0.01; // 10ms resolution
    double beats = 0.0;
    for (double t = 0.0; t < timeSec; t += step) {
        double bpm = getTempoAt(t);
        beats += bpm / 60.0 * step;
    }
    return beats;
}

} // namespace BeatMate::Core
