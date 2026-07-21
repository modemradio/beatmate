#include "FlangerProcessor.h"
#include <cmath>
#include <algorithm>

namespace BeatMate::Core {

static constexpr double kPi = 3.14159265358979323846;
static constexpr float kMaxDelayMs = 10.0f;

FlangerProcessor::FlangerProcessor() {
    bufferSize_ = static_cast<int>(kMaxDelayMs * 0.001f * sampleRate_) + 2;
    delayBufferL_.resize(bufferSize_, 0.0f);
    delayBufferR_.resize(bufferSize_, 0.0f);
}

FlangerProcessor::~FlangerProcessor() = default;

void FlangerProcessor::setRate(float hz) { rate_.store(std::clamp(hz, 0.05f, 10.0f)); }
void FlangerProcessor::setDepth(float d) { depth_.store(std::clamp(d, 0.0f, 1.0f)); }
void FlangerProcessor::setFeedback(float f) { feedback_.store(std::clamp(f, -0.95f, 0.95f)); }
void FlangerProcessor::setMix(float m) { mix_.store(std::clamp(m, 0.0f, 1.0f)); }

void FlangerProcessor::process(float* buffer, int numSamples, int channels) {
    float rate = rate_.load();
    float depth = depth_.load();
    float fb = feedback_.load();
    float mx = mix_.load();
    double phaseInc = rate / sampleRate_;
    float maxDelay = kMaxDelayMs * 0.001f * static_cast<float>(sampleRate_);
    bool stereo = channels >= 2;

    for (int i = 0; i < numSamples; ++i) {
        float lfo = static_cast<float>(0.5 + 0.5 * std::sin(2.0 * kPi * lfoPhase_));
        float delaySamples = 1.0f + lfo * depth * maxDelay * 0.5f;

        int idx0 = (writePos_ - static_cast<int>(delaySamples) + bufferSize_) % bufferSize_;
        int idx1 = (idx0 + 1) % bufferSize_;
        float frac = delaySamples - std::floor(delaySamples);

        float dryL = buffer[i * channels];
        float wetL = delayBufferL_[idx0] * (1.0f - frac) + delayBufferL_[idx1] * frac;
        delayBufferL_[writePos_] = dryL + wetL * fb;
        buffer[i * channels] = dryL * (1.0f - mx) + wetL * mx;

        if (stereo) {
            float lfo2 = static_cast<float>(0.5 + 0.5 * std::sin(2.0 * kPi * (lfoPhase_ + 0.25)));
            float delaySamples2 = 1.0f + lfo2 * depth * maxDelay * 0.5f;
            int idx0r = (writePos_ - static_cast<int>(delaySamples2) + bufferSize_) % bufferSize_;
            int idx1r = (idx0r + 1) % bufferSize_;
            float fracR = delaySamples2 - std::floor(delaySamples2);

            float dryR = buffer[i * channels + 1];
            float wetR = delayBufferR_[idx0r] * (1.0f - fracR) + delayBufferR_[idx1r] * fracR;
            delayBufferR_[writePos_] = dryR + wetR * fb;
            buffer[i * channels + 1] = dryR * (1.0f - mx) + wetR * mx;
        }

        lfoPhase_ += phaseInc;
        if (lfoPhase_ >= 1.0) lfoPhase_ -= 1.0;
        writePos_ = (writePos_ + 1) % bufferSize_;
    }
}

void FlangerProcessor::reset() {
    std::fill(delayBufferL_.begin(), delayBufferL_.end(), 0.0f);
    std::fill(delayBufferR_.begin(), delayBufferR_.end(), 0.0f);
    writePos_ = 0;
    lfoPhase_ = 0.0;
}

} // namespace BeatMate::Core
