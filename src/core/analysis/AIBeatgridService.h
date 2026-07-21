#pragma once

#include "BeatGridGenerator.h"
#include <vector>
#include <string>

namespace BeatMate::Core {

class AudioTrack;

struct AIBeatgridResult {
    BeatGrid grid;
    float confidence = 0.0f;
    bool isVariableTempo = false;
    std::vector<double> tempoChanges;     // Times where tempo changes
    std::vector<double> tempoValues;      // BPM at each change point
    std::string method;                    // "hybrid", "onset", "spectral"
    double processingTimeMs = 0.0;
};

class AIBeatgridService {
public:
    AIBeatgridService();
    ~AIBeatgridService();

    AIBeatgridResult generate(const AudioTrack& track);

    void setMinBPM(double min) { minBPM_ = min; }
    void setMaxBPM(double max) { maxBPM_ = max; }
    void setUseML(bool use) { useML_ = use; }

private:
    // Multi-feature onset detection (combines spectral flux, phase deviation, complex domain)
    std::vector<double> multiFeatureOnsetDetection(const AudioTrack& track);

    // Tempo estimation using multiple methods
    double multiMethodTempoEstimation(const std::vector<double>& onsets, int sampleRate);

    // Dynamic programming beat tracking with variable tempo support
    std::vector<double> dpBeatTracking(const std::vector<double>& onsets, double initialTempo,
                                        int sampleRate);

    // Refine grid using onset alignment
    BeatGrid refineGrid(const std::vector<double>& beats, const std::vector<double>& onsets,
                         double bpm);

    double minBPM_ = 60.0;
    double maxBPM_ = 200.0;
    bool useML_ = false;
};

} // namespace BeatMate::Core
