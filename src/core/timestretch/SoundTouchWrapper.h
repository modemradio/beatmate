#pragma once
#include <memory>
namespace BeatMate::Core {
class AudioTrack;
class SoundTouchWrapper {
public:
    SoundTouchWrapper();
    ~SoundTouchWrapper();
    void initialize(int sampleRate, int channels);
    void setTempo(double ratio);
    void setPitch(double semitones);
    void setRate(double ratio);
    std::shared_ptr<AudioTrack> process(const AudioTrack& input);
    void reset();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    int sampleRate_ = 44100;
    int channels_ = 2;
};
} // namespace BeatMate::Core
