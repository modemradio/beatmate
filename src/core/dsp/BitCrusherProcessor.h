#pragma once

#include "DSPProcessor.h"
#include <atomic>

namespace BeatMate::Core {

class BitCrusherProcessor : public DSPProcessor {
public:
    BitCrusherProcessor();
    ~BitCrusherProcessor() override;

    void process(float* buffer, int numSamples, int channels) override;
    void reset() override;
    std::string getName() const override { return "BitCrusher"; }

    void setBitDepth(int bits);
    void setSampleRateReduction(float targetRate);
    void setMix(float mix);

    int getBitDepth() const { return bitDepth_.load(); }
    float getSampleRateReduction() const { return targetRate_.load(); }
    float getMix() const { return mix_.load(); }

private:
    std::atomic<int> bitDepth_{8};
    std::atomic<float> targetRate_{8000.0f};
    std::atomic<float> mix_{1.0f};

    float holdSampleL_ = 0.0f;
    float holdSampleR_ = 0.0f;
    float holdCounter_ = 0.0f;
};

}
