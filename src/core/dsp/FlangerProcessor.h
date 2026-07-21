#pragma once

#include "DSPProcessor.h"
#include <atomic>
#include <vector>

namespace BeatMate::Core {

class FlangerProcessor : public DSPProcessor {
public:
    FlangerProcessor();
    ~FlangerProcessor() override;

    void process(float* buffer, int numSamples, int channels) override;
    void reset() override;
    std::string getName() const override { return "Flanger"; }

    void setRate(float hz);
    void setDepth(float depth);
    void setFeedback(float feedback);
    void setMix(float mix);

    float getRate() const { return rate_.load(); }
    float getDepth() const { return depth_.load(); }
    float getFeedback() const { return feedback_.load(); }
    float getMix() const { return mix_.load(); }

private:
    std::atomic<float> rate_{0.5f};
    std::atomic<float> depth_{0.7f};
    std::atomic<float> feedback_{0.5f};
    std::atomic<float> mix_{0.5f};

    std::vector<float> delayBufferL_, delayBufferR_;
    int writePos_ = 0;
    int bufferSize_ = 0;
    double lfoPhase_ = 0.0;
    float lastFeedbackL_ = 0.0f, lastFeedbackR_ = 0.0f;
};

} // namespace BeatMate::Core
