#pragma once

#include "DSPProcessor.h"
#include <atomic>
#include <vector>

namespace BeatMate::Core {

class ChorusProcessor : public DSPProcessor {
public:
    ChorusProcessor();
    ~ChorusProcessor() override;

    void process(float* buffer, int numSamples, int channels) override;
    void reset() override;
    std::string getName() const override { return "Chorus"; }

    void setRate(float hz);      // 0.1 to 10 Hz
    void setDepth(float depth);  // 0 to 1
    void setMix(float mix);      // 0 to 1
    void setVoices(int voices);  // 1 to 4

    float getRate() const { return rate_.load(); }
    float getDepth() const { return depth_.load(); }
    float getMix() const { return mix_.load(); }

private:
    std::atomic<float> rate_{1.0f};
    std::atomic<float> depth_{0.5f};
    std::atomic<float> mix_{0.5f};
    int numVoices_ = 2;

    std::vector<float> delayBuffer_;
    int writePos_ = 0;
    int bufferSize_ = 0;

    struct LFO {
        double phase = 0.0;
        double phaseOffset = 0.0;
    };
    std::vector<LFO> lfos_;
};

} // namespace BeatMate::Core
