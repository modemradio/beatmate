#include "DelayProcessor.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace BeatMate::Core {

DelayProcessor::DelayProcessor()
    : feedbackFilter_(std::make_unique<FilterProcessor>()) {
    maxDelaySamples_ = static_cast<int>(5.0 * sampleRate_); // 5 seconds max
    delayBufferL_.resize(maxDelaySamples_, 0.0f);
    delayBufferR_.resize(maxDelaySamples_, 0.0f);

    feedbackFilter_->setType(FilterType::LowPass);
    feedbackFilter_->setFrequency(8000.0f);
    feedbackFilter_->setQ(0.707f);
}

DelayProcessor::~DelayProcessor() = default;

void DelayProcessor::setDelayTime(float ms) {
    delayTimeMs_.store(std::clamp(ms, 1.0f, 5000.0f));
}

void DelayProcessor::setFeedback(float feedback) {
    feedback_.store(std::clamp(feedback, 0.0f, 0.95f));
}

void DelayProcessor::setMix(float mix) {
    mix_.store(std::clamp(mix, 0.0f, 1.0f));
}

void DelayProcessor::setBPMSync(float bpm, float division) {
    if (bpm <= 0.0f) return;
    float beatMs = 60000.0f / bpm;
    float delayMs = beatMs * division * 4.0f; // division relative to whole note
    setDelayTime(delayMs);
}

void DelayProcessor::setFeedbackFilterFreq(float freq) {
    feedbackFilter_->setFrequency(freq);
}

void DelayProcessor::setFeedbackFilterType(FilterType type) {
    feedbackFilter_->setType(type);
}

void DelayProcessor::process(float* buffer, int numSamples, int channels) {
    int delaySamples = static_cast<int>(delayTimeMs_.load() * 0.001f * sampleRate_);
    delaySamples = std::clamp(delaySamples, 1, maxDelaySamples_ - 1);

    float fb = feedback_.load();
    float mx = mix_.load();
    float dryGain = 1.0f - mx;
    bool pp = pingPong_.load();
    bool stereo = channels >= 2;

    for (int i = 0; i < numSamples; ++i) {
        int readPos = (writePos_ - delaySamples + maxDelaySamples_) % maxDelaySamples_;

        float dryL = buffer[i * channels];
        float dryR = stereo ? buffer[i * channels + 1] : dryL;

        float wetL = delayBufferL_[readPos];
        float wetR = delayBufferR_[readPos];

        float filteredL = wetL;
        float filteredR = wetR;
        if (stereo) {
            float fbBuf[2] = { wetL, wetR };
            feedbackFilter_->process(fbBuf, 1, 2);
            filteredL = fbBuf[0];
            filteredR = fbBuf[1];
        } else {
            feedbackFilter_->process(&filteredL, 1, 1);
        }

        if (pp && stereo) {
            // Ping-pong: cross-feed L->R and R->L
            delayBufferL_[writePos_] = dryL + filteredR * fb;
            delayBufferR_[writePos_] = dryR + filteredL * fb;
        } else {
            delayBufferL_[writePos_] = dryL + filteredL * fb;
            delayBufferR_[writePos_] = dryR + filteredR * fb;
        }

        buffer[i * channels] = dryL * dryGain + wetL * mx;
        if (stereo) {
            buffer[i * channels + 1] = dryR * dryGain + wetR * mx;
        }

        writePos_ = (writePos_ + 1) % maxDelaySamples_;
    }
}

void DelayProcessor::reset() {
    std::fill(delayBufferL_.begin(), delayBufferL_.end(), 0.0f);
    std::fill(delayBufferR_.begin(), delayBufferR_.end(), 0.0f);
    writePos_ = 0;
    feedbackFilter_->reset();
}

} // namespace BeatMate::Core
