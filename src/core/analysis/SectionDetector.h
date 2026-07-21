#pragma once

#include "StructureDetector.h"
#include <vector>

namespace BeatMate::Core {

class AudioTrack;

class SectionDetector {
public:
    SectionDetector();
    ~SectionDetector();

    std::vector<Section> detect(const AudioTrack& track, double bpm = 0.0);

    std::vector<double> detectChanges(const AudioTrack& track);

private:
    float computeSpectralDifference(const std::vector<float>& specA,
                                     const std::vector<float>& specB);
};

} // namespace BeatMate::Core
