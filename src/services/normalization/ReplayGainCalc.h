#pragma once
#include <vector>
#include "../../core/audio/AudioTrack.h"
namespace BeatMate::Services::Normalization {
class ReplayGainCalc {
public:
    ReplayGainCalc() = default;
    float calculate(const Core::AudioTrack& track);
    float calculateAlbumGain(const std::vector<Core::AudioTrack>& tracks);
    static constexpr float REFERENCE_LEVEL = -18.0f; // dBFS
};
} // namespace BeatMate::Services::Normalization
