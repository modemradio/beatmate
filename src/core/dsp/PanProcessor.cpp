#include "PanProcessor.h"
#include <cmath>
#include <algorithm>

namespace BeatMate::Core {

static constexpr float kPiOver2 = 1.5707963267948966f;

PanProcessor::PanProcessor() = default;
PanProcessor::~PanProcessor() = default;

void PanProcessor::setPan(float pan) {
    pan_.store(std::clamp(pan, -1.0f, 1.0f));
}

void PanProcessor::process(float* buffer, int numSamples, int channels) {
    if (channels < 2) return; // Pan only makes sense for stereo+

    float p = pan_.load();

    // Equal-power panning law
    float angle = (p + 1.0f) * 0.25f * kPiOver2 * 2.0f; // 0 to pi/2
    float gainL = std::cos(angle);
    float gainR = std::sin(angle);

    for (int i = 0; i < numSamples; ++i) {
        float l = buffer[i * channels];
        float r = buffer[i * channels + 1];

        buffer[i * channels]     = l * gainL;
        buffer[i * channels + 1] = r * gainR;
    }
}

} // namespace BeatMate::Core
