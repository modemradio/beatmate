#pragma once

#include "DSPProcessor.h"
#include <atomic>

namespace BeatMate::Core {

class PanProcessor : public DSPProcessor {
public:
    PanProcessor();
    ~PanProcessor() override;

    void process(float* buffer, int numSamples, int channels) override;
    void reset() override {}
    std::string getName() const override { return "Pan"; }

    void setPan(float pan); // -1.0 (left) to 1.0 (right)
    float getPan() const { return pan_.load(); }

private:
    std::atomic<float> pan_{0.0f};
};

} // namespace BeatMate::Core
