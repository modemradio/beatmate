#pragma once

#include "DSPProcessor.h"
#include <atomic>

namespace BeatMate::Core {

class GainProcessor : public DSPProcessor {
public:
    GainProcessor();
    ~GainProcessor() override;

    void process(float* buffer, int numSamples, int channels) override;
    void reset() override;
    std::string getName() const override { return "Gain"; }

    void setGain(float dB);
    float getGain() const { return gainDb_.load(); }

    // Auto-gain: target LUFS level
    void setAutoGain(bool enabled) { autoGainEnabled_.store(enabled); }
    bool isAutoGainEnabled() const { return autoGainEnabled_.load(); }
    void setTargetLUFS(float lufs) { targetLUFS_.store(lufs); }

    // Ramp time in ms to avoid clicks
    void setRampTime(float ms) { rampTimeMs_.store(ms); }

private:
    std::atomic<float> gainDb_{0.0f};
    std::atomic<float> targetLUFS_{-14.0f};
    std::atomic<bool> autoGainEnabled_{false};
    std::atomic<float> rampTimeMs_{5.0f};

    float currentGainLin_ = 1.0f;
    float targetGainLin_ = 1.0f;

    // For LUFS measurement
    float rmsAccumulator_ = 0.0f;
    int rmsCount_ = 0;
};

} // namespace BeatMate::Core
