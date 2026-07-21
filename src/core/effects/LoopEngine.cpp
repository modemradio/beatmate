#include "LoopEngine.h"
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

LoopEngine::LoopEngine() = default;
LoopEngine::~LoopEngine() = default;

void LoopEngine::setLoop(double startSec, double endSec) {
    if (endSec > startSec) {
        // Publish start/end before flipping active_ so the audio thread never sees a stale range
        loopStart_.store(startSec, std::memory_order_relaxed);
        loopEnd_.store(endSec,   std::memory_order_relaxed);
        active_.store(true,      std::memory_order_release);
        spdlog::info("LoopEngine: set loop {:.3f} - {:.3f}s", startSec, endSec);
    }
}

void LoopEngine::clearLoop() {
    active_.store(false, std::memory_order_release);
    spdlog::info("LoopEngine: loop cleared");
}

void LoopEngine::setLoopRoll(double division, double bpm, double currentPosition) {
    if (bpm <= 0.0) return;
    double beatDuration = 60.0 / bpm;
    double loopLength = beatDuration * division;
    setLoop(currentPosition, currentPosition + loopLength);
}

void LoopEngine::setAutoLoop(int bars, double bpm, double currentPosition) {
    if (bpm <= 0.0) return;
    double barDuration = 60.0 / bpm * 4.0;
    double loopLength = barDuration * bars;
    const double firstBeat = firstBeat_.load(std::memory_order_acquire);
    double barPos = firstBeat + std::floor((currentPosition - firstBeat) / barDuration) * barDuration;
    setLoop(barPos, barPos + loopLength);
}

void LoopEngine::setBeatGrid(double firstBeatOffset) {
    firstBeat_.store(firstBeatOffset, std::memory_order_release);
}

double LoopEngine::processPosition(double position) const {
    if (!active_.load(std::memory_order_acquire)) return position;
    const double start = loopStart_.load(std::memory_order_acquire);
    const double end   = loopEnd_.load(std::memory_order_acquire);
    if (position < start) return start;
    if (position >= end) {
        double loopLen = end - start;
        if (loopLen > 0) {
            return start + std::fmod(position - start, loopLen);
        }
    }
    return position;
}

} // namespace BeatMate::Core
