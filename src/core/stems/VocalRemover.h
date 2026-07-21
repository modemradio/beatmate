#pragma once
#include <memory>
namespace BeatMate::Core {
class AudioTrack;
class VocalRemover {
public:
    VocalRemover();
    ~VocalRemover();
    std::shared_ptr<AudioTrack> remove(const AudioTrack& track);
    std::shared_ptr<AudioTrack> removeSimple(const AudioTrack& track);
};
} // namespace BeatMate::Core
