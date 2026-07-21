#include "NoiseGate.h"
#include <cmath>
#include <algorithm>

namespace BeatMate::Core {

NoiseGate::NoiseGate() {
    attackCoeff_ = std::exp(-1.0f / (0.001f * attack_.load() * static_cast<float>(sampleRate_)));
    releaseCoeff_ = std::exp(-1.0f / (0.001f * release_.load() * static_cast<float>(sampleRate_)));
}

NoiseGate::~NoiseGate() = default;

void NoiseGate::setThreshold(float dB) {
    threshold_.store(std::clamp(dB, -80.0f, 0.0f));
}

void NoiseGate::setAttack(float ms) {
    attack_.store(std::clamp(ms, 0.1f, 50.0f));
    attackCoeff_ = std::exp(-1.0f / (0.001f * ms * static_cast<float>(sampleRate_)));
}

void NoiseGate::setRelease(float ms) {
    release_.store(std::clamp(ms, 5.0f, 1000.0f));
    releaseCoeff_ = std::exp(-1.0f / (0.001f * ms * static_cast<float>(sampleRate_)));
}

void NoiseGate::setHold(float ms) {
    hold_.store(std::clamp(ms, 0.0f, 500.0f));
}

void NoiseGate::process(float* buffer, int numSamples, int channels) {
    float threshLin = std::pow(10.0f, threshold_.load() / 20.0f);
    int holdSamples = static_cast<int>(hold_.load() * 0.001f * sampleRate_);

    for (int i = 0; i < numSamples; ++i) {
        float peak = 0.0f;
        for (int ch = 0; ch < channels; ++ch) {
            float abs_s = std::fabs(buffer[i * channels + ch]);
            if (abs_s > peak) peak = abs_s;
        }

        bool aboveThreshold = peak > threshLin;

        switch (state_) {
            case GateState::Closed:
                if (aboveThreshold) {
                    state_ = GateState::Attack;
                }
                break;

            case GateState::Attack:
                envelope_ = attackCoeff_ * envelope_ + (1.0f - attackCoeff_) * 1.0f;
                if (envelope_ >= 0.999f) {
                    envelope_ = 1.0f;
                    state_ = GateState::Open;
                    gateOpen_.store(true);
                }
                if (!aboveThreshold) {
                    state_ = GateState::Hold;
                    holdCounter_ = holdSamples;
                }
                break;

            case GateState::Open:
                envelope_ = 1.0f;
                if (!aboveThreshold) {
                    state_ = GateState::Hold;
                    holdCounter_ = holdSamples;
                }
                break;

            case GateState::Hold:
                envelope_ = 1.0f;
                if (aboveThreshold) {
                    state_ = GateState::Open;
                } else {
                    holdCounter_--;
                    if (holdCounter_ <= 0) {
                        state_ = GateState::Release;
                    }
                }
                break;

            case GateState::Release:
                envelope_ = releaseCoeff_ * envelope_;
                if (envelope_ < 0.001f) {
                    envelope_ = 0.0f;
                    state_ = GateState::Closed;
                    gateOpen_.store(false);
                }
                if (aboveThreshold) {
                    state_ = GateState::Attack;
                }
                break;
        }

        for (int ch = 0; ch < channels; ++ch) {
            buffer[i * channels + ch] *= envelope_;
        }
    }
}

void NoiseGate::reset() {
    envelope_ = 0.0f;
    state_ = GateState::Closed;
    holdCounter_ = 0;
    gateOpen_.store(false);
}

}
