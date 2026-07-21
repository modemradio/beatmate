#include "AutoDuck.h"
#include <cmath>
#include <algorithm>

namespace BeatMate::Core {

AutoDuck::AutoDuck() = default;
AutoDuck::~AutoDuck() = default;

void AutoDuck::processSidechain(const float* sidechain, int numSamples, int channels) {
    float peak = 0.0f;
    for (int i = 0; i < numSamples * channels; ++i) {
        float abs_s = std::fabs(sidechain[i]);
        if (abs_s > peak) peak = abs_s;
    }
    sidechainLevel_ = (peak > 1e-10f) ? 20.0f * std::log10(peak) : -200.0f;
}

void AutoDuck::process(float* buffer, int numSamples, int channels) {
    float thresh = threshold_.load();
    float amountDb = amount_.load();
    float attackMs = attack_.load();
    float releaseMs = release_.load();

    float attackCoeff = std::exp(-1.0f / (0.001f * attackMs * static_cast<float>(sampleRate_)));
    float releaseCoeff = std::exp(-1.0f / (0.001f * releaseMs * static_cast<float>(sampleRate_)));

    bool ducking = sidechainLevel_ > thresh;
    float targetGain = ducking ? std::pow(10.0f, amountDb / 20.0f) : 1.0f;

    for (int i = 0; i < numSamples; ++i) {
        if (targetGain < envelope_) {
            envelope_ = attackCoeff * envelope_ + (1.0f - attackCoeff) * targetGain;
        } else {
            envelope_ = releaseCoeff * envelope_ + (1.0f - releaseCoeff) * targetGain;
        }
        for (int ch = 0; ch < channels; ++ch) {
            buffer[i * channels + ch] *= envelope_;
        }
    }
}

void AutoDuck::reset() { envelope_ = 1.0f; sidechainLevel_ = -200.0f; }

} // namespace BeatMate::Core
