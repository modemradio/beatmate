#pragma once
#include <cstdint>
#include <vector>
#include "../../models/CuePoint.h"
#include "../../core/audio/AudioTrack.h"

namespace BeatMate::Services::AI {

class IntelligentCueCreator {
public:
    IntelligentCueCreator() = default;
    std::vector<Models::CuePoint> createCues(const Core::AudioTrack& track, int64_t trackId);
private:
    double findFirstBeat(const Core::AudioTrack& track) const;
    double findDrop(const Core::AudioTrack& track) const;
    double findBreakdown(const Core::AudioTrack& track) const;
    double findOutro(const Core::AudioTrack& track) const;
};

} // namespace BeatMate::Services::AI
