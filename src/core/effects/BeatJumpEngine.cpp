#include "BeatJumpEngine.h"
#include <cmath>
#include <algorithm>

namespace BeatMate::Core {

void BeatJumpEngine::setBeatGrid(double firstBeatOffset, double bpm) {
    firstBeat_.store(firstBeatOffset, std::memory_order_relaxed);
    bpm_.store(bpm,                   std::memory_order_relaxed);
    gridValid_.store(bpm > 0.0,       std::memory_order_release);
}

void BeatJumpEngine::setMaxDuration(double seconds) {
    maxDuration_.store(seconds > 0.0 ? seconds : 0.0, std::memory_order_release);
}

double BeatJumpEngine::jump(double currentPosition, int beats, double bpm) {
    if (bpm <= 0.0) return currentPosition;
    double beatDuration = 60.0 / bpm;
    double rawTarget = currentPosition + beats * beatDuration;
    double newPos = quantizeToGrid(rawTarget);
    const double maxDur = maxDuration_.load(std::memory_order_acquire);
    if (maxDur > 0.0) {
        return std::max(0.0, std::min(maxDur, newPos));
    }
    return std::max(0.0, newPos);
}

double BeatJumpEngine::quantizeToGrid(double position) const {
    if (!gridValid_.load(std::memory_order_acquire)) return position;
    const double bpm   = bpm_.load(std::memory_order_acquire);
    const double first = firstBeat_.load(std::memory_order_acquire);
    if (bpm <= 0) return position;
    double beatDuration = 60.0 / bpm;
    double beatsFromFirst = (position - first) / beatDuration;
    double nearestBeat = std::round(beatsFromFirst);
    return first + nearestBeat * beatDuration;
}

}
