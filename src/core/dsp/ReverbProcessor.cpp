#include "ReverbProcessor.h"
#include <algorithm>

namespace BeatMate::Core {

static const int kCombTuning[] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
static const int kAllPassTuning[] = { 556, 441, 341, 225 };

ReverbProcessor::ReverbProcessor() {
    initFilters();
}

ReverbProcessor::~ReverbProcessor() = default;

void ReverbProcessor::initFilters() {
    double scale = sampleRate_ / 44100.0;

    for (int i = 0; i < kNumCombs; ++i) {
        int size = static_cast<int>(kCombTuning[i] * scale);
        combL_[i].init(size);
        combR_[i].init(size + kStereoSpread);
    }

    for (int i = 0; i < kNumAllPasses; ++i) {
        int size = static_cast<int>(kAllPassTuning[i] * scale);
        allPassL_[i].init(size);
        allPassR_[i].init(size + kStereoSpread);
    }
}

void ReverbProcessor::setRoomSize(float size) {
    roomSize_.store(std::clamp(size, 0.0f, 1.0f));
}

void ReverbProcessor::setDamping(float damp) {
    damping_.store(std::clamp(damp, 0.0f, 1.0f));
}

void ReverbProcessor::setWet(float wet) {
    wet_.store(std::clamp(wet, 0.0f, 1.0f));
}

void ReverbProcessor::setDry(float dry) {
    dry_.store(std::clamp(dry, 0.0f, 1.0f));
}

void ReverbProcessor::setWidth(float width) {
    width_.store(std::clamp(width, 0.0f, 1.0f));
}

void ReverbProcessor::process(float* buffer, int numSamples, int channels) {
    float roomSize = roomSize_.load() * 0.28f + 0.7f;
    float damp = damping_.load();
    float wet = wet_.load();
    float dry = dry_.load();
    float width = width_.load();

    float wet1 = wet * (width / 2.0f + 0.5f);
    float wet2 = wet * ((1.0f - width) / 2.0f);

    bool stereo = (channels >= 2);

    for (int i = 0; i < numSamples; ++i) {
        float inputL = buffer[i * channels];
        float inputR = stereo ? buffer[i * channels + 1] : inputL;
        float input = (inputL + inputR) * 0.015f;

        float outL = 0.0f, outR = 0.0f;

        for (int c = 0; c < kNumCombs; ++c) {
            outL += combL_[c].process(input, roomSize, damp);
            outR += combR_[c].process(input, roomSize, damp);
        }

        for (int a = 0; a < kNumAllPasses; ++a) {
            outL = allPassL_[a].process(outL);
            outR = allPassR_[a].process(outR);
        }

        if (stereo) {
            buffer[i * channels]     = inputL * dry + outL * wet1 + outR * wet2;
            buffer[i * channels + 1] = inputR * dry + outR * wet1 + outL * wet2;
        } else {
            buffer[i * channels] = inputL * dry + (outL + outR) * 0.5f * wet;
        }
    }
}

void ReverbProcessor::reset() {
    for (auto& c : combL_) c.clear();
    for (auto& c : combR_) c.clear();
    for (auto& a : allPassL_) a.clear();
    for (auto& a : allPassR_) a.clear();
}

}
