#include "AutoGain.h"
#include "../audio/AudioTrack.h"
#include "../analysis/LoudnessAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

float AutoGain::calculateGain(const AudioTrack& track, float targetLUFS) {
    LoudnessAnalyzer analyzer;
    auto result = analyzer.analyze(track);

    float correction = targetLUFS - result.integratedLUFS;
    correction = std::clamp(correction, -12.0f, 12.0f);

    spdlog::info("AutoGain: track at {:.1f} LUFS, target {:.1f} LUFS, correction {:.1f} dB",
                 result.integratedLUFS, targetLUFS, correction);
    return correction;
}

} // namespace BeatMate::Core
