#pragma once
#include "../dsp/DSPProcessor.h"
#include <atomic>
namespace BeatMate::Core {
class AutoDuck : public DSPProcessor {
public:
    AutoDuck();
    ~AutoDuck() override;
    void process(float* buffer, int numSamples, int channels) override;
    void reset() override;
    std::string getName() const override { return "Auto Duck"; }
    void processSidechain(const float* sidechain, int numSamples, int channels);
    void setThreshold(float dB) { threshold_.store(dB); }
    void setAmount(float dB) { amount_.store(dB); }
    void setAttack(float ms) { attack_.store(ms); }
    void setRelease(float ms) { release_.store(ms); }
private:
    std::atomic<float> threshold_{-20.0f};
    std::atomic<float> amount_{-12.0f};
    std::atomic<float> attack_{5.0f};
    std::atomic<float> release_{100.0f};
    float envelope_ = 0.0f;
    float sidechainLevel_ = 0.0f;
};
} // namespace BeatMate::Core
