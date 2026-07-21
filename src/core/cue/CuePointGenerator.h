#pragma once
#include "HotCueManager.h"
#include <vector>
namespace BeatMate::Core {
class AudioTrack;
class CuePointGenerator {
public:
    CuePointGenerator();
    ~CuePointGenerator();
    std::vector<CuePoint> generateCues(const AudioTrack& track, int maxCues = 8);
};
} // namespace BeatMate::Core
