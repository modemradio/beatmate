#include "DistortionProcessor.h"
#include <cmath>
#include <algorithm>

namespace BeatMate::Core {

DistortionProcessor::DistortionProcessor() = default;
DistortionProcessor::~DistortionProcessor() = default;

void DistortionProcessor::setDrive(float d) { drive_.store(std::clamp(d, 0.0f, 1.0f)); }
void DistortionProcessor::setTone(float t) { tone_.store(std::clamp(t, 0.0f, 1.0f)); }
void DistortionProcessor::setMix(float m) { mix_.store(std::clamp(m, 0.0f, 1.0f)); }

float DistortionProcessor::processOverdrive(float sample, float drive) const {
    float gain = 1.0f + drive * 20.0f;
    return std::tanh(sample * gain) / std::tanh(gain);
}

float DistortionProcessor::processFuzz(float sample, float drive) const {
    float gain = 1.0f + drive * 50.0f;
    float s = sample * gain;
    if (s > 0.0f) {
        s = 1.0f - std::exp(-s);
    } else {
        s = -(1.0f - std::exp(s)) * 1.2f;
    }
    return s;
}

float DistortionProcessor::processTube(float sample, float drive) const {
    float gain = 1.0f + drive * 15.0f;
    float s = sample * gain;

    float output = (2.0f / 3.14159f) * std::atan(s);

    float even = s * std::fabs(s) * 0.1f * drive;
    output += even;

    return std::clamp(output, -1.0f, 1.0f);
}

void DistortionProcessor::process(float* buffer, int numSamples, int channels) {
    float drv = drive_.load();
    float tone = tone_.load();
    float mx = mix_.load();

    float toneCoeff = 0.05f + tone * 0.9f;

    for (int i = 0; i < numSamples; ++i) {
        for (int ch = 0; ch < channels; ++ch) {
            float dry = buffer[i * channels + ch];
            float wet;

            switch (type_) {
                case DistortionType::Overdrive:
                    wet = processOverdrive(dry, drv);
                    break;
                case DistortionType::Fuzz:
                    wet = processFuzz(dry, drv);
                    break;
                case DistortionType::Tube:
                    wet = processTube(dry, drv);
                    break;
            }

            toneFilterState_ = toneFilterState_ + toneCoeff * (wet - toneFilterState_);
            wet = toneFilterState_ * (1.0f - tone) + wet * tone;

            buffer[i * channels + ch] = dry * (1.0f - mx) + wet * mx;
        }
    }
}

void DistortionProcessor::reset() {
    toneFilterState_ = 0.0f;
}

} // namespace BeatMate::Core
