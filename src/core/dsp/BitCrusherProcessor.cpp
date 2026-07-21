#include "BitCrusherProcessor.h"
#include <cmath>
#include <algorithm>

namespace BeatMate::Core {

BitCrusherProcessor::BitCrusherProcessor() = default;
BitCrusherProcessor::~BitCrusherProcessor() = default;

void BitCrusherProcessor::setBitDepth(int bits) {
    bitDepth_.store(std::clamp(bits, 1, 16));
}

void BitCrusherProcessor::setSampleRateReduction(float rate) {
    targetRate_.store(std::clamp(rate, 100.0f, static_cast<float>(sampleRate_)));
}

void BitCrusherProcessor::setMix(float m) {
    mix_.store(std::clamp(m, 0.0f, 1.0f));
}

void BitCrusherProcessor::process(float* buffer, int numSamples, int channels) {
    int bits = bitDepth_.load();
    float targetRate = targetRate_.load();
    float mx = mix_.load();

    // Quantization levels
    float levels = std::pow(2.0f, static_cast<float>(bits));
    float halfLevels = levels / 2.0f;

    // Sample rate reduction ratio
    float rateRatio = targetRate / static_cast<float>(sampleRate_);
    bool stereo = channels >= 2;

    for (int i = 0; i < numSamples; ++i) {
        holdCounter_ += rateRatio;

        if (holdCounter_ >= 1.0f) {
            holdCounter_ -= 1.0f;

            // Sample and hold
            float sampleL = buffer[i * channels];
            // Bit depth reduction (quantize)
            holdSampleL_ = std::round(sampleL * halfLevels) / halfLevels;

            if (stereo) {
                float sampleR = buffer[i * channels + 1];
                holdSampleR_ = std::round(sampleR * halfLevels) / halfLevels;
            }
        }

        // Output held (downsampled + quantized) sample
        float dryL = buffer[i * channels];
        buffer[i * channels] = dryL * (1.0f - mx) + holdSampleL_ * mx;

        if (stereo) {
            float dryR = buffer[i * channels + 1];
            buffer[i * channels + 1] = dryR * (1.0f - mx) + holdSampleR_ * mx;
        }
    }
}

void BitCrusherProcessor::reset() {
    holdSampleL_ = holdSampleR_ = 0.0f;
    holdCounter_ = 0.0f;
}

} // namespace BeatMate::Core
