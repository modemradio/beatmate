#pragma once
#include <memory>
#include <string>
namespace BeatMate::Core {
class AudioTrack;
enum class StretchQuality { Fast, Medium, HighQuality };
class TimeStretchEngine {
public:
    TimeStretchEngine();
    ~TimeStretchEngine();
    std::shared_ptr<AudioTrack> stretch(const AudioTrack& track, double ratio,
                                         StretchQuality quality = StretchQuality::HighQuality);
    std::shared_ptr<AudioTrack> pitchShift(const AudioTrack& track, double semitones);
    void setPreserveFormants(bool v) { preserveFormants_ = v; }
private:
    bool preserveFormants_ = true;
};
} // namespace BeatMate::Core
