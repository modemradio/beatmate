#pragma once
#include "../dsp/DSPProcessor.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace BeatMate::Core {

class EffectsChain {
public:
    EffectsChain();
    ~EffectsChain();

    int addEffect(std::unique_ptr<DSPProcessor> effect);
    bool removeEffect(int index);
    void moveEffect(int from, int to);
    void clear();

    void process(float* buffer, int numSamples, int channels);

    DSPProcessor* getEffect(int index);
    int getEffectCount() const;

    void setBypass(bool bypass) { bypassed_.store(bypass); }
    bool isBypassed() const { return bypassed_.load(); }

    void setSampleRate(double sr);

private:
    std::vector<std::unique_ptr<DSPProcessor>> effects_;
    mutable std::mutex mutex_;
    std::atomic<bool> bypassed_{false};
};

} // namespace BeatMate::Core
