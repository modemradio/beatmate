#pragma once
#include <memory>
namespace BeatMate::Core {
class AudioTrack;
class PitchShifter {
public:
    PitchShifter();
    ~PitchShifter();
    std::shared_ptr<AudioTrack> shift(const AudioTrack& input, double semitones);
    void setPreserveFormants(bool v) { preserveFormants_ = v; }
private:
    bool preserveFormants_ = true;
};
} // namespace BeatMate::Core
