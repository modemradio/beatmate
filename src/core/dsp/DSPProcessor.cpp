#include "DSPProcessor.h"
#include <cstring>
#include <vector>

namespace BeatMate::Core {

void DSPProcessor::processWithMix(float* buffer, int numSamples, int channels) {
    if (bypassed_.load()) return;

    float mix = wetDry_.load();

    if (mix >= 0.999f) {
        process(buffer, numSamples, channels);
        return;
    }

    if (mix <= 0.001f) {
        return;
    }

    int totalSamples = numSamples * channels;
    std::vector<float> dry(buffer, buffer + totalSamples);

    process(buffer, numSamples, channels);

    float wet = mix;
    float dryGain = 1.0f - mix;
    for (int i = 0; i < totalSamples; ++i) {
        buffer[i] = buffer[i] * wet + dry[i] * dryGain;
    }
}

} // namespace BeatMate::Core
