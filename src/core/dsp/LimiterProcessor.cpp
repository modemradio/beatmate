#include "LimiterProcessor.h"
#include <cmath>
#include <algorithm>

namespace BeatMate::Core {

LimiterProcessor::LimiterProcessor() {
    delaySamples_ = static_cast<int>(lookaheadMs_ * 0.001 * sampleRate_);
    releaseCoeff_ = std::exp(-1.0f / (0.001f * release_.load() * static_cast<float>(sampleRate_)));
}

LimiterProcessor::~LimiterProcessor() = default;

void LimiterProcessor::setCeiling(float dB) {
    ceiling_.store(std::clamp(dB, -6.0f, 0.0f));
}

void LimiterProcessor::setRelease(float ms) {
    release_.store(std::clamp(ms, 1.0f, 1000.0f));
    releaseCoeff_ = std::exp(-1.0f / (0.001f * ms * static_cast<float>(sampleRate_)));
}

void LimiterProcessor::setLookahead(float ms) {
    lookaheadMs_ = std::clamp(ms, 0.0f, 10.0f);
    delaySamples_ = static_cast<int>(lookaheadMs_ * 0.001 * sampleRate_);
}

void LimiterProcessor::process(float* buffer, int numSamples, int channels) {
    float ceilingLin = std::pow(10.0f, ceiling_.load() / 20.0f);
    float maxGR = 0.0f;
    float maxPeak = 0.0f;

    if (static_cast<int>(delayBuffer_.size()) != channels ||
        (delaySamples_ > 0 && static_cast<int>(delayBuffer_[0].size()) != delaySamples_ + 1)) {
        delayBuffer_.resize(channels);
        for (auto& db : delayBuffer_) {
            db.resize(delaySamples_ + 1, 0.0f);
        }
        delayWritePos_ = 0;
    }

    for (int i = 0; i < numSamples; ++i) {
        float peak = 0.0f;
        for (int ch = 0; ch < channels; ++ch) {
            float abs_s = std::fabs(buffer[i * channels + ch]);
            if (abs_s > peak) peak = abs_s;
        }

        if (peak > maxPeak) maxPeak = peak;

        float targetGain = 1.0f;
        if (peak > ceilingLin && peak > 1e-10f) {
            targetGain = ceilingLin / peak;
        }

        if (targetGain < envelope_) {
            envelope_ = targetGain; // instant attack
        } else {
            envelope_ = releaseCoeff_ * envelope_ + (1.0f - releaseCoeff_) * targetGain;
        }

        float gain = std::min(1.0f, envelope_);
        float gr = 20.0f * std::log10(std::max(gain, 1e-10f));
        if (gr < maxGR) maxGR = gr;

        if (delaySamples_ > 0) {
            int readPos = (delayWritePos_ - delaySamples_ + static_cast<int>(delayBuffer_[0].size()))
                          % static_cast<int>(delayBuffer_[0].size());
            for (int ch = 0; ch < channels; ++ch) {
                delayBuffer_[ch][delayWritePos_] = buffer[i * channels + ch];
                buffer[i * channels + ch] = delayBuffer_[ch][readPos] * gain;
            }
            delayWritePos_ = (delayWritePos_ + 1) % static_cast<int>(delayBuffer_[0].size());
        } else {
            for (int ch = 0; ch < channels; ++ch) {
                buffer[i * channels + ch] *= gain;
            }
        }
    }

    gainReduction_.store(-maxGR);
    truePeak_.store(20.0f * std::log10(std::max(maxPeak, 1e-10f)));
}

void LimiterProcessor::reset() {
    envelope_ = 1.0f;
    gainReduction_.store(0.0f);
    truePeak_.store(-200.0f);
    delayWritePos_ = 0;
    for (auto& db : delayBuffer_) {
        std::fill(db.begin(), db.end(), 0.0f);
    }
}

} // namespace BeatMate::Core
