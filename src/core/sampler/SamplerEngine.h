#pragma once
#include "SamplePad.h"
#include <array>
#include <string>
#include <vector>
namespace BeatMate::Core {
enum class SamplerMode { Samples, Loops, Sections, Automation, Effects };
class SamplerEngine {
public:
    SamplerEngine();
    ~SamplerEngine();
    bool loadSample(int padIndex, const std::string& path);
    void triggerPad(int index);
    void stopPad(int index);
    void stopAll();
    void processBlock(float* output, int numFrames, int channels);
    SamplePad* getPad(int index);
    void setPadMode(int index, PadMode mode);
    void setPadVolume(int index, float vol);
    void setPadPitch(int index, float semitones);
    void setSamplerMode(SamplerMode mode) { mode_ = mode; }
    SamplerMode getSamplerMode() const { return mode_; }
    static constexpr int kNumPads = 16;
private:
    std::array<SamplePad, kNumPads> pads_;
    SamplerMode mode_ = SamplerMode::Samples;
    std::vector<float> mixBuffer_;
};
} // namespace BeatMate::Core
