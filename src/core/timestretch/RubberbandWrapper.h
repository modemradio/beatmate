#pragma once
#include <memory>
namespace BeatMate::Core {
class AudioTrack;
class RubberbandWrapper {
public:
    RubberbandWrapper();
    ~RubberbandWrapper();
    void initialize(int sampleRate, int channels);
    void setTimeRatio(double ratio);
    void setPitchScale(double scale);
    std::shared_ptr<AudioTrack> process(const AudioTrack& input);
    static bool isAvailable();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    int sampleRate_ = 44100;
    int channels_ = 2;
    double timeRatio_ = 1.0;
    double pitchScale_ = 1.0;
};
} // namespace BeatMate::Core
