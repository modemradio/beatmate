#pragma once
#include <memory>
namespace BeatMate::Core {
class AudioTrack;
class DrumIsolator {
public:
    DrumIsolator();
    ~DrumIsolator();
    std::shared_ptr<AudioTrack> isolate(const AudioTrack& track);
};
} // namespace BeatMate::Core
