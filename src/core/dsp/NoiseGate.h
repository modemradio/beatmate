#pragma once

#include "DSPProcessor.h"
#include <atomic>

namespace BeatMate::Core {

class NoiseGate : public DSPProcessor {
public:
    NoiseGate();
    ~NoiseGate() override;

    void process(float* buffer, int numSamples, int channels) override;
    void reset() override;
    std::string getName() const override { return "Noise Gate"; }

    void setThreshold(float dB);   // -80 to 0 dB
    void setAttack(float ms);      // 0.1 to 50 ms
    void setRelease(float ms);     // 5 to 1000 ms
    void setHold(float ms);        // 0 to 500 ms

    float getThreshold() const { return threshold_.load(); }
    float getAttack() const { return attack_.load(); }
    float getRelease() const { return release_.load(); }
    float getHold() const { return hold_.load(); }

    bool isOpen() const { return gateOpen_.load(); }

private:
    std::atomic<float> threshold_{-40.0f};
    std::atomic<float> attack_{1.0f};
    std::atomic<float> release_{50.0f};
    std::atomic<float> hold_{10.0f};

    std::atomic<bool> gateOpen_{false};

    enum class GateState { Closed, Attack, Open, Hold, Release };
    GateState state_ = GateState::Closed;

    float envelope_ = 0.0f;
    int holdCounter_ = 0;
    float attackCoeff_ = 0.0f;
    float releaseCoeff_ = 0.0f;
};

} // namespace BeatMate::Core
