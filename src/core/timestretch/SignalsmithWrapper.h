#pragma once
#include <memory>
namespace BeatMate::Core {
class AudioTrack;
class SignalsmithWrapper {
public:
    SignalsmithWrapper();
    ~SignalsmithWrapper();
    void initialize(int sampleRate, int channels);
    void setStretchFactor(double factor);
    void setPitchFactor(double factor);
    std::shared_ptr<AudioTrack> process(const AudioTrack& input);
    void reset();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    int sampleRate_ = 44100;
    int channels_ = 2;
    double stretchFactor_ = 1.0;
    double pitchFactor_ = 1.0;
};
} // namespace BeatMate::Core
