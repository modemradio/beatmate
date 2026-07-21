#pragma once

#include "DSPProcessor.h"
#include <atomic>
#include <vector>

namespace BeatMate::Core {

class LimiterProcessor : public DSPProcessor {
public:
    LimiterProcessor();
    ~LimiterProcessor() override;

    void process(float* buffer, int numSamples, int channels) override;
    void reset() override;
    std::string getName() const override { return "Limiter"; }

    void setCeiling(float dB);      // -6 to 0 dB
    void setRelease(float ms);      // 1 to 1000 ms
    void setLookahead(float ms);    // 0 to 10 ms

    float getCeiling() const { return ceiling_.load(); }
    float getRelease() const { return release_.load(); }

    float getGainReduction() const { return gainReduction_.load(); }
    float getTruePeak() const { return truePeak_.load(); }

private:
    std::atomic<float> ceiling_{-0.3f};
    std::atomic<float> release_{50.0f};
    float lookaheadMs_ = 5.0f;

    std::atomic<float> gainReduction_{0.0f};
    std::atomic<float> truePeak_{0.0f};

    float envelope_ = 0.0f;
    float releaseCoeff_ = 0.0f;

    std::vector<std::vector<float>> delayBuffer_;
    int delayWritePos_ = 0;
    int delaySamples_ = 0;
};

} // namespace BeatMate::Core
