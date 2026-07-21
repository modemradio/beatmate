#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <cstdint>
namespace BeatMate::Core {
class AudioTrack;
enum class PadMode { OneShot, Loop, Gate };
class SamplePad {
public:
    SamplePad();
    ~SamplePad();
    void loadSample(std::shared_ptr<AudioTrack> sample);
    void trigger();
    void stop();
    void processBlock(float* output, int numFrames, int channels);
    bool isPlaying() const { return playing_.load(); }
    void setMode(PadMode mode) { mode_ = mode; }
    PadMode getMode() const { return mode_; }
    void setVolume(float vol) { volume_.store(vol); }
    void setPitch(float semitones) { pitch_.store(semitones); }
    void setColor(uint32_t color) { color_ = color; }
    uint32_t getColor() const { return color_; }
    void setName(const std::string& name) { name_ = name; }
    std::string getName() const { return name_; }
    bool hasSample() const { return sample_ != nullptr; }
private:
    std::shared_ptr<AudioTrack> sample_;
    PadMode mode_ = PadMode::OneShot;
    std::atomic<bool> playing_{false};
    std::atomic<float> volume_{1.0f};
    std::atomic<float> pitch_{0.0f};
    std::atomic<double> position_{0.0};
    uint32_t color_ = 0xFF808080;
    std::string name_;
};
} // namespace BeatMate::Core
