#include "ChorusProcessor.h"
#include <cmath>
#include <algorithm>

namespace BeatMate::Core {

static constexpr double kPi = 3.14159265358979323846;
static constexpr float kMaxDelayMs = 50.0f;

ChorusProcessor::ChorusProcessor() {
    bufferSize_ = static_cast<int>(kMaxDelayMs * 0.001f * sampleRate_) + 1;
    delayBuffer_.resize(bufferSize_, 0.0f);
    setVoices(2);
}

ChorusProcessor::~ChorusProcessor() = default;

void ChorusProcessor::setRate(float hz) {
    rate_.store(std::clamp(hz, 0.1f, 10.0f));
}

void ChorusProcessor::setDepth(float depth) {
    depth_.store(std::clamp(depth, 0.0f, 1.0f));
}

void ChorusProcessor::setMix(float mix) {
    mix_.store(std::clamp(mix, 0.0f, 1.0f));
}

void ChorusProcessor::setVoices(int voices) {
    numVoices_ = std::clamp(voices, 1, 4);
    lfos_.resize(numVoices_);
    for (int i = 0; i < numVoices_; ++i) {
        lfos_[i].phaseOffset = static_cast<double>(i) / numVoices_;
    }
}

void ChorusProcessor::process(float* buffer, int numSamples, int channels) {
    float rate = rate_.load();
    float depth = depth_.load();
    float mx = mix_.load();
    float phaseInc = static_cast<float>(rate / sampleRate_);
    float maxDelaySamples = kMaxDelayMs * 0.001f * static_cast<float>(sampleRate_);
    float baseDelay = maxDelaySamples * 0.5f;

    for (int i = 0; i < numSamples; ++i) {
        float dryL = buffer[i * channels];

        delayBuffer_[writePos_] = dryL;

        float wetSum = 0.0f;
        for (int v = 0; v < numVoices_; ++v) {
            double phase = lfos_[v].phase + lfos_[v].phaseOffset;
            float lfoVal = static_cast<float>(std::sin(2.0 * kPi * phase));
            float delaySamples = baseDelay + lfoVal * depth * baseDelay * 0.5f;
            delaySamples = std::clamp(delaySamples, 1.0f, static_cast<float>(bufferSize_ - 2));

            int idx0 = (writePos_ - static_cast<int>(delaySamples) + bufferSize_) % bufferSize_;
            int idx1 = (idx0 + 1) % bufferSize_;
            float frac = delaySamples - std::floor(delaySamples);

            wetSum += delayBuffer_[idx0] * (1.0f - frac) + delayBuffer_[idx1] * frac;
        }
        wetSum /= numVoices_;

        for (auto& lfo : lfos_) {
            lfo.phase += phaseInc;
            if (lfo.phase >= 1.0) lfo.phase -= 1.0;
        }

        float wetL = dryL * (1.0f - mx) + wetSum * mx;
        buffer[i * channels] = wetL;

        if (channels >= 2) {
            buffer[i * channels + 1] = buffer[i * channels + 1] * (1.0f - mx) + wetSum * mx * 0.9f;
        }

        writePos_ = (writePos_ + 1) % bufferSize_;
    }
}

void ChorusProcessor::reset() {
    std::fill(delayBuffer_.begin(), delayBuffer_.end(), 0.0f);
    writePos_ = 0;
    for (auto& lfo : lfos_) lfo.phase = 0.0;
}

}
