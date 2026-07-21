#pragma once
#include "../dsp/DSPProcessor.h"
#include <memory>
#include <string>

namespace BeatMate::Core {

class EffectSlot {
public:
    EffectSlot();
    ~EffectSlot();

    void setEffect(std::unique_ptr<DSPProcessor> effect);
    DSPProcessor* getEffect() { return effect_.get(); }
    const DSPProcessor* getEffect() const { return effect_.get(); }

    void process(float* buffer, int numSamples, int channels);

    void setBypass(bool v) { bypassed_.store(v); }
    bool isBypassed() const { return bypassed_.load(); }

    void setWetDry(float mix) { wetDry_.store(mix); }
    float getWetDry() const { return wetDry_.load(); }

    bool hasEffect() const { return effect_ != nullptr; }
    std::string getEffectName() const;

private:
    std::unique_ptr<DSPProcessor> effect_;
    std::atomic<bool> bypassed_{false};
    std::atomic<float> wetDry_{1.0f};
};

} // namespace BeatMate::Core
