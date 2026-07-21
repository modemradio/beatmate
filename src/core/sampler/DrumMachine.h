#pragma once
#include "SamplerEngine.h"
#include <array>
#include <atomic>
namespace BeatMate::Core {
class DrumMachine {
public:
    DrumMachine();
    ~DrumMachine();
    static constexpr int kSteps = 16;
    static constexpr int kTracks = 8;
    void setStep(int track, int step, bool active);
    bool getStep(int track, int step) const;
    void setBPM(double bpm) { bpm_.store(bpm); }
    double getBPM() const { return bpm_.load(); }
    void start();
    void stop();
    bool isPlaying() const { return playing_.load(); }
    void processBlock(float* output, int numFrames, int channels, int sampleRate);
    bool loadKit(int track, const std::string& samplePath);
private:
    std::array<std::array<bool, kSteps>, kTracks> pattern_{};
    SamplerEngine sampler_;
    std::atomic<double> bpm_{120.0};
    std::atomic<bool> playing_{false};
    double stepPosition_ = 0.0;
    int currentStep_ = 0;
};
} // namespace BeatMate::Core
