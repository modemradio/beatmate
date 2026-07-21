#include "TransitionEditor.h"

#include <algorithm>

namespace BeatMate::Services::Preparation {

namespace {
constexpr double kDefaultOverlap = 16.0;

bool isSet(double v) { return v >= 0.0; }
}

TransitionPlan TransitionEditor::suggestDefault(const Models::Track& a, const Models::Track& b) {
    TransitionPlan plan;
    plan.trackAId = a.id;
    plan.trackBId = b.id;

    double overlap = kDefaultOverlap;

    double aMixOut = 0.0;
    if (isSet(a.outroStart)) {
        aMixOut = a.outroStart;
        const double aEnd = isSet(a.outroEnd) ? a.outroEnd
                          : (a.duration > 0.0 ? a.duration : a.outroStart + kDefaultOverlap);
        overlap = std::max(1.0, aEnd - aMixOut);
    } else {
        if (a.duration > 0.0) {
            aMixOut = std::max(0.0, a.duration - kDefaultOverlap);
            overlap = std::min(kDefaultOverlap, a.duration);
        } else {
            aMixOut = 0.0;
            overlap = kDefaultOverlap;
        }
    }

    double bMixIn = 0.0;
    if (isSet(b.introStart)) {
        bMixIn = b.introStart;
        if (isSet(b.introEnd)) {
            const double bOverlap = std::max(1.0, b.introEnd - b.introStart);
            overlap = std::min(overlap, bOverlap);
        }
    } else {
        bMixIn = 0.0;
    }

    plan.mixOutStart = aMixOut;
    plan.mixInStart  = bMixIn;
    plan.overlapSec  = overlap;
    plan.crossfadeCurve = 0.5f;
    return plan;
}

}
