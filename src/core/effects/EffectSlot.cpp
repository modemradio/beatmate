#include "EffectSlot.h"
#include <cstring>
#include <vector>

namespace BeatMate::Core {

EffectSlot::EffectSlot() = default;
EffectSlot::~EffectSlot() = default;

void EffectSlot::setEffect(std::unique_ptr<DSPProcessor> effect) {
    effect_ = std::move(effect);
}

std::string EffectSlot::getEffectName() const {
    return effect_ ? effect_->getName() : "Empty";
}

void EffectSlot::process(float* buffer, int numSamples, int channels) {
    if (!effect_ || bypassed_.load()) return;

    float mix = wetDry_.load();
    if (mix < 0.001f) return;

    if (mix >= 0.999f) {
        effect_->process(buffer, numSamples, channels);
        return;
    }

    int total = numSamples * channels;
    std::vector<float> dry(buffer, buffer + total);
    effect_->process(buffer, numSamples, channels);

    for (int i = 0; i < total; ++i) {
        buffer[i] = dry[i] * (1.0f - mix) + buffer[i] * mix;
    }
}

} // namespace BeatMate::Core
