#pragma once
#include <vector>
namespace BeatMate::Core {
class AudioTrack;
struct EQSettings { float lowGain = 0; float midGain = 0; float highGain = 0; };
class EQMatcher {
public:
    EQMatcher() = default;
    EQSettings match(const AudioTrack& trackA, const AudioTrack& trackB);
private:
    std::vector<float> computeSpectralProfile(const AudioTrack& track);
};
}
