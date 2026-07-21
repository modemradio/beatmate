#pragma once

#include <vector>

namespace BeatMate::Core {

class AudioTrack;

struct EnergyResult {
    int overall = 5;              // 1-10
    std::vector<float> curve;     // Energy per segment (0-1)
    double segmentDuration = 0.5; // seconds per segment
    float rmsAverage = 0.0f;
    float spectralCentroid = 0.0f;
};

struct EnergySection {
    double startSec = 0.0;
    double endSec = 0.0;
    int energy = 1;
};

class EnergyAnalyzer {
public:
    EnergyAnalyzer();
    ~EnergyAnalyzer();

    EnergyResult analyze(const AudioTrack& track, double segmentDuration = 0.5);

    static EnergyResult fromRmsCurve(std::vector<float> rmsPerSegment,
                                     double segmentDuration,
                                     float spectralCentroid = 0.0f);

    static std::vector<EnergySection> sectionize(const EnergyResult& result,
                                                 double minSectionSec = 8.0);

private:
    float computeRMS(const float* data, size_t numSamples) const;
    float computeSpectralCentroid(const float* data, size_t numSamples, int sampleRate) const;
};

} // namespace BeatMate::Core
