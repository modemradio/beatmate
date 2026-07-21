#include "CompressorProcessor.h"
#include <cmath>
#include <algorithm>

namespace BeatMate::Core {

CompressorProcessor::CompressorProcessor() {
    attackCoeff_ = std::exp(-1.0f / (0.001f * attack_.load() * static_cast<float>(sampleRate_)));
    releaseCoeff_ = std::exp(-1.0f / (0.001f * release_.load() * static_cast<float>(sampleRate_)));
}

CompressorProcessor::~CompressorProcessor() = default;

void CompressorProcessor::setThreshold(float dB) {
    threshold_.store(std::clamp(dB, -60.0f, 0.0f));
}

void CompressorProcessor::setRatio(float ratio) {
    ratio_.store(std::clamp(ratio, 1.0f, 20.0f));
}

void CompressorProcessor::setAttack(float ms) {
    attack_.store(std::clamp(ms, 0.1f, 200.0f));
    attackCoeff_ = std::exp(-1.0f / (0.001f * ms * static_cast<float>(sampleRate_)));
}

void CompressorProcessor::setRelease(float ms) {
    release_.store(std::clamp(ms, 5.0f, 2000.0f));
    releaseCoeff_ = std::exp(-1.0f / (0.001f * ms * static_cast<float>(sampleRate_)));
}

void CompressorProcessor::setMakeupGain(float dB) {
    makeupGain_.store(std::clamp(dB, 0.0f, 40.0f));
}

void CompressorProcessor::setKnee(float dB) {
    knee_.store(std::clamp(dB, 0.0f, 20.0f));
}

void CompressorProcessor::setMixAmount(float amount0to1) noexcept {
    // Clamp et map 0..1 vers tous les paramètres en une passe.
    const float a = std::clamp(amount0to1, 0.0f, 1.0f);
    setThreshold(-6.0f + (-30.0f - -6.0f) * a);
    setRatio(1.0f + 7.0f * a);
    setAttack(30.0f - 25.0f * a);
    setRelease(250.0f - 200.0f * a);
    setMakeupGain(6.0f * a);
    setKnee(6.0f - 4.0f * a);
}

float CompressorProcessor::computeGain(float inputLevel_dB) const {
    float thresh = threshold_.load();
    float rat = ratio_.load();
    float kn = knee_.load();

    float outputLevel_dB;

    if (kn <= 0.0f) {
        // Hard knee
        if (inputLevel_dB <= thresh) {
            outputLevel_dB = inputLevel_dB;
        } else {
            outputLevel_dB = thresh + (inputLevel_dB - thresh) / rat;
        }
    } else {
        // Soft knee
        float halfKnee = kn / 2.0f;
        if (inputLevel_dB < (thresh - halfKnee)) {
            outputLevel_dB = inputLevel_dB;
        } else if (inputLevel_dB > (thresh + halfKnee)) {
            outputLevel_dB = thresh + (inputLevel_dB - thresh) / rat;
        } else {
            // In the knee region - quadratic interpolation
            float x = inputLevel_dB - thresh + halfKnee;
            outputLevel_dB = inputLevel_dB + ((1.0f / rat) - 1.0f) * x * x / (2.0f * kn);
        }
    }

    return outputLevel_dB - inputLevel_dB; // gain reduction in dB
}

void CompressorProcessor::process(float* buffer, int numSamples, int channels) {
    if (bypassed_.load()) {
        return;
    }

    float makeupLin = std::pow(10.0f, makeupGain_.load() / 20.0f);
    float maxGR = 0.0f;

    for (int i = 0; i < numSamples; ++i) {
        float peak = 0.0f;
        for (int ch = 0; ch < channels; ++ch) {
            float abs_s = std::fabs(buffer[i * channels + ch]);
            if (abs_s > peak) peak = abs_s;
        }

        float inputdB = (peak > 1e-10f) ? 20.0f * std::log10(peak) : -200.0f;

        float targetGR = computeGain(inputdB);

        // Envelope follower (smooth the gain reduction)
        if (targetGR < envelope_) {
            envelope_ = attackCoeff_ * envelope_ + (1.0f - attackCoeff_) * targetGR;
        } else {
            envelope_ = releaseCoeff_ * envelope_ + (1.0f - releaseCoeff_) * targetGR;
        }

        float gainLin = std::pow(10.0f, envelope_ / 20.0f) * makeupLin;
        for (int ch = 0; ch < channels; ++ch) {
            buffer[i * channels + ch] *= gainLin;
        }

        if (envelope_ < maxGR) maxGR = envelope_;
    }

    gainReduction_.store(-maxGR); // Store as positive value
}

void CompressorProcessor::reset() {
    envelope_ = 0.0f;
    gainReduction_.store(0.0f);
}

} // namespace BeatMate::Core
