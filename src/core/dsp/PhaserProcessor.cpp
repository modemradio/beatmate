#include "PhaserProcessor.h"
#include <cmath>
#include <algorithm>

namespace BeatMate::Core {

static constexpr double kPi = 3.14159265358979323846;

PhaserProcessor::PhaserProcessor() = default;
PhaserProcessor::~PhaserProcessor() = default;

void PhaserProcessor::setRate(float hz) { rate_.store(std::clamp(hz, 0.05f, 10.0f)); }
void PhaserProcessor::setDepth(float d) { depth_.store(std::clamp(d, 0.0f, 1.0f)); }
void PhaserProcessor::setFeedback(float f) { feedback_.store(std::clamp(f, -0.95f, 0.95f)); }
void PhaserProcessor::setStages(int s) { stages_ = std::clamp(s, 2, kMaxStages); }

void PhaserProcessor::process(float* buffer, int numSamples, int channels) {
    float rate = rate_.load();
    float depth = depth_.load();
    float fb = feedback_.load();
    double phaseInc = rate / sampleRate_;
    bool stereo = channels >= 2;

    float minFreq = 200.0f;
    float maxFreq = 6000.0f;

    for (int i = 0; i < numSamples; ++i) {
        float lfo = static_cast<float>(0.5 + 0.5 * std::sin(2.0 * kPi * lfoPhase_));
        float freq = minFreq + lfo * depth * (maxFreq - minFreq);

        float w = static_cast<float>(std::tan(kPi * freq / sampleRate_));
        float coeff = (w - 1.0f) / (w + 1.0f);

        float inputL = buffer[i * channels] + lastOutputL_ * fb;
        float outputL = inputL;
        for (int s = 0; s < stages_; ++s) {
            float ap_out = coeff * outputL + statesL_[s].y1;
            statesL_[s].y1 = outputL - coeff * ap_out;
            outputL = ap_out;
        }
        lastOutputL_ = outputL;
        buffer[i * channels] = buffer[i * channels] * (1.0f - depth * 0.5f) + outputL * depth * 0.5f;

        if (stereo) {
            // Offset LFO for stereo
            float lfo2 = static_cast<float>(0.5 + 0.5 * std::sin(2.0 * kPi * (lfoPhase_ + 0.5)));
            float freq2 = minFreq + lfo2 * depth * (maxFreq - minFreq);
            float w2 = static_cast<float>(std::tan(kPi * freq2 / sampleRate_));
            float coeff2 = (w2 - 1.0f) / (w2 + 1.0f);

            float inputR = buffer[i * channels + 1] + lastOutputR_ * fb;
            float outputR = inputR;
            for (int s = 0; s < stages_; ++s) {
                float ap_out = coeff2 * outputR + statesR_[s].y1;
                statesR_[s].y1 = outputR - coeff2 * ap_out;
                outputR = ap_out;
            }
            lastOutputR_ = outputR;
            buffer[i * channels + 1] = buffer[i * channels + 1] * (1.0f - depth * 0.5f) + outputR * depth * 0.5f;
        }

        lfoPhase_ += phaseInc;
        if (lfoPhase_ >= 1.0) lfoPhase_ -= 1.0;
    }
}

void PhaserProcessor::reset() {
    for (auto& s : statesL_) s.y1 = 0.0f;
    for (auto& s : statesR_) s.y1 = 0.0f;
    lfoPhase_ = 0.0;
    lastOutputL_ = lastOutputR_ = 0.0f;
}

} // namespace BeatMate::Core
