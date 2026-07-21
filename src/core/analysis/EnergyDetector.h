#pragma once

#include <vector>

namespace BeatMate::Core {

class AudioTrack;

// Energie 1-10 compatible Mixed In Key : 5 = seuil dansable, 10 = peak time.
class EnergyDetector {
public:
    EnergyDetector() = default;

    struct EnergySegment {
        double startTime = 0.0;
        double endTime = 0.0;
        int energy = 1;     // 1-10
    };

    struct EnergyResult {
        int overallEnergy = 1;                  // 1-10
        float rawScore = 0.0f;                  // 0.0-1.0
        std::vector<EnergySegment> segments;    // per-section energy
    };

    EnergyResult detect(const AudioTrack& track, double bpm = 0.0);

private:
    float computeRMS(const float* data, size_t numSamples);
    float computeOnsetRate(const float* data, size_t numSamples, int sr);
    float computeSpectralCentroid(const float* data, size_t numSamples, int sr);
    float computeBassRatio(const float* data, size_t numSamples, int sr);
    float computeHighFreqEnergy(const float* data, size_t numSamples, int sr);
    int scoreToEnergy(float score);
};

} // namespace BeatMate::Core
