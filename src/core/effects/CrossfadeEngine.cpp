#include "CrossfadeEngine.h"
#include <algorithm>
#include <cmath>

namespace BeatMate::Core {

static constexpr float kPi = 3.14159265358979f;

float CrossfadeEngine::getCurve(float position, CrossfadeType type, bool isA) {
    float p = std::clamp(position, 0.0f, 1.0f);
    float gain;

    switch (type) {
        case CrossfadeType::Linear:
            gain = isA ? (1.0f - p) : p;
            break;
        case CrossfadeType::EqualPower:
            gain = isA ? std::cos(p * kPi / 2.0f) : std::sin(p * kPi / 2.0f);
            break;
        case CrossfadeType::SCurve: {
            float s = 0.5f * (1.0f - std::cos(p * kPi));
            gain = isA ? (1.0f - s) : s;
            break;
        }
        case CrossfadeType::ConstantPower: {
            float angle = p * kPi / 2.0f;
            gain = isA
                ? std::sqrt(std::max(0.0f, std::cos(angle)))
                : std::sqrt(std::max(0.0f, std::sin(angle)));
            break;
        }
    }
    return gain;
}

void CrossfadeEngine::crossfade(const float* trackA, const float* trackB, float* output,
                                 int numSamples, int channels, float position, CrossfadeType type) {
    float gainA = getCurve(position, type, true);
    float gainB = getCurve(position, type, false);

    int total = numSamples * channels;
    for (int i = 0; i < total; ++i) {
        output[i] = trackA[i] * gainA + trackB[i] * gainB;
    }
}

} // namespace BeatMate::Core
