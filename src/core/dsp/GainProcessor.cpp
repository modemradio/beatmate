#include "GainProcessor.h"
#include <cmath>
#include <algorithm>

namespace BeatMate::Core {

GainProcessor::GainProcessor() = default;
GainProcessor::~GainProcessor() = default;

void GainProcessor::setGain(float dB) {
    gainDb_.store(std::clamp(dB, -60.0f, 24.0f));
    targetGainLin_ = std::pow(10.0f, gainDb_.load() / 20.0f);
}

void GainProcessor::process(float* buffer, int numSamples, int channels) {
    float rampMs = rampTimeMs_.load();
    int rampSamples = static_cast<int>(rampMs * 0.001f * sampleRate_);
    rampSamples = std::max(1, rampSamples);

    float gainIncrement = (targetGainLin_ - currentGainLin_) / rampSamples;

    for (int i = 0; i < numSamples; ++i) {
        if (std::fabs(currentGainLin_ - targetGainLin_) > 1e-6f) {
            currentGainLin_ += gainIncrement;
            if ((gainIncrement > 0 && currentGainLin_ > targetGainLin_) ||
                (gainIncrement < 0 && currentGainLin_ < targetGainLin_)) {
                currentGainLin_ = targetGainLin_;
            }
        } else {
            currentGainLin_ = targetGainLin_;
        }

        for (int ch = 0; ch < channels; ++ch) {
            buffer[i * channels + ch] *= currentGainLin_;
        }

        if (autoGainEnabled_.load()) {
            float sumSq = 0.0f;
            for (int ch = 0; ch < channels; ++ch) {
                float s = buffer[i * channels + ch];
                sumSq += s * s;
            }
            rmsAccumulator_ += sumSq / channels;
            rmsCount_++;

            if (rmsCount_ >= static_cast<int>(sampleRate_ * 0.4)) {
                float rms = std::sqrt(rmsAccumulator_ / rmsCount_);
                float currentLUFS = (rms > 1e-10f) ? 20.0f * std::log10(rms) - 0.691f : -200.0f;
                float correction = targetLUFS_.load() - currentLUFS;
                correction = std::clamp(correction, -6.0f, 6.0f);
                targetGainLin_ = std::pow(10.0f, (gainDb_.load() + correction) / 20.0f);

                rmsAccumulator_ = 0.0f;
                rmsCount_ = 0;
            }
        }
    }
}

void GainProcessor::reset() {
    currentGainLin_ = targetGainLin_;
    rmsAccumulator_ = 0.0f;
    rmsCount_ = 0;
}

} // namespace BeatMate::Core
