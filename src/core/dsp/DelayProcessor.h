#pragma once

#include "DSPProcessor.h"
#include "FilterProcessor.h"
#include <atomic>
#include <memory>
#include <vector>

namespace BeatMate::Core {

class DelayProcessor : public DSPProcessor {
public:
    DelayProcessor();
    ~DelayProcessor() override;

    void process(float* buffer, int numSamples, int channels) override;
    void reset() override;
    std::string getName() const override { return "Delay"; }

    void setDelayTime(float ms);       // 1 to 5000 ms
    void setFeedback(float feedback);  // 0 to 0.95
    void setMix(float mix);            // 0 to 1

    void setPingPong(bool enabled) { pingPong_.store(enabled); }
    bool isPingPong() const { return pingPong_.load(); }

    void setBPMSync(float bpm, float division); // division: 1=quarter, 0.5=eighth
    void setFeedbackFilterFreq(float freq);
    void setFeedbackFilterType(FilterType type);

    float getDelayTime() const { return delayTimeMs_.load(); }
    float getFeedback() const { return feedback_.load(); }
    float getMix() const { return mix_.load(); }

private:
    std::atomic<float> delayTimeMs_{500.0f};
    std::atomic<float> feedback_{0.5f};
    std::atomic<float> mix_{0.5f};
    std::atomic<bool> pingPong_{false};

    std::vector<float> delayBufferL_;
    std::vector<float> delayBufferR_;
    int writePos_ = 0;
    int maxDelaySamples_ = 0;

    std::unique_ptr<FilterProcessor> feedbackFilter_;
};

} // namespace BeatMate::Core
