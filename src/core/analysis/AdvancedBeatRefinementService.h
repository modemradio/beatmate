#pragma once

#include "BeatGridGenerator.h"
#include <vector>

namespace BeatMate::Core {

class AudioTrack;

struct RefinedBeatGrid {
    BeatGrid grid;
    std::vector<double> corrections;    // Per-beat correction in ms
    double averageDeviation = 0.0;      // Average beat deviation in ms
    double maxDeviation = 0.0;          // Max deviation in ms
    float gridConfidence = 0.0f;        // 0-1
    bool isConsistent = false;          // True if grid is stable
};

class AdvancedBeatRefinementService {
public:
    AdvancedBeatRefinementService();
    ~AdvancedBeatRefinementService();

    RefinedBeatGrid refine(const AudioTrack& track, const BeatGrid& rawGrid);

    RefinedBeatGrid correctDrift(const AudioTrack& track, const BeatGrid& grid);

    std::vector<double> snapToOnsets(const std::vector<double>& beats,
                                     const std::vector<double>& onsets,
                                     double maxShiftMs = 20.0);

    void setMaxCorrectionMs(double ms) { maxCorrectionMs_ = ms; }
    void setOnsetWeight(double w) { onsetWeight_ = w; }

private:
    std::vector<float> computeOnsetStrengths(const AudioTrack& track,
                                              const std::vector<double>& beatPositions);

    BeatGrid iterativeAlign(const AudioTrack& track, const BeatGrid& grid, int iterations);

    std::vector<double> detectDrift(const std::vector<double>& beats, double expectedBPM);

    double maxCorrectionMs_ = 30.0;
    double onsetWeight_ = 0.7;
};

} // namespace BeatMate::Core
