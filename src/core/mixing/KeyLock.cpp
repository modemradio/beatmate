#include "KeyLock.h"
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

void KeyLock::lock(const std::string& originalKey) {
    lockedKey_ = originalKey;
    locked_.store(true);
    spdlog::info("KeyLock: locked to {}", originalKey);
}

void KeyLock::unlock() {
    locked_.store(false);
    spdlog::info("KeyLock: unlocked");
}

double KeyLock::getPitchCorrection(double tempoRatio) const {
    if (!locked_.load() || tempoRatio <= 0) return 0.0;
    // When tempo changes by a ratio, pitch changes by log2(ratio) * 12 semitones
    double pitchShiftSemitones = 12.0 * std::log2(tempoRatio);
    return -pitchShiftSemitones; // Inverse to compensate
}

} // namespace BeatMate::Core
