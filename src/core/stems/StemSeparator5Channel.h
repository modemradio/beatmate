#pragma once

#include "StemSeparator.h"

namespace BeatMate::Core {

struct StemResult5 {
    std::shared_ptr<AudioTrack> vocals;
    std::shared_ptr<AudioTrack> drums;
    std::shared_ptr<AudioTrack> bass;
    std::shared_ptr<AudioTrack> melody;
    std::shared_ptr<AudioTrack> other;
    bool success = false;
};

class StemSeparator5Channel {
public:
    StemSeparator5Channel();
    ~StemSeparator5Channel();

    StemResult5 separate(const AudioTrack& track, StemProgressCallback progress = nullptr);

private:
    std::unique_ptr<StemSeparator> baseSeparator_;
};

} // namespace BeatMate::Core
