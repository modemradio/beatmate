#pragma once

#include "DSPProcessor.h"
#include <atomic>

namespace BeatMate::Core {

enum class DistortionType { Overdrive, Fuzz, Tube };

class DistortionProcessor : public DSPProcessor {
public:
    DistortionProcessor();
    ~DistortionProcessor() override;

    void process(float* buffer, int numSamples, int channels) override;
    void reset() override;
    std::string getName() const override { return "Distortion"; }

    void setType(DistortionType type) { type_ = type; }
    DistortionType getType() const { return type_; }

    void setDrive(float drive);    // 0 to 1
    void setTone(float tone);      // 0 to 1 (low to high)
    void setMix(float mix);        // 0 to 1

    float getDrive() const { return drive_.load(); }
    float getTone() const { return tone_.load(); }
    float getMix() const { return mix_.load(); }

private:
    float processOverdrive(float sample, float drive) const;
    float processFuzz(float sample, float drive) const;
    float processTube(float sample, float drive) const;

    DistortionType type_ = DistortionType::Overdrive;
    std::atomic<float> drive_{0.5f};
    std::atomic<float> tone_{0.5f};
    std::atomic<float> mix_{1.0f};

    // Simple one-pole filter state for tone control
    float toneFilterState_ = 0.0f;
};

} // namespace BeatMate::Core
