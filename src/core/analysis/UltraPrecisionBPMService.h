#pragma once

#include "BPMDetector.h"
#include <vector>
#include <string>

namespace BeatMate::Core {

class AudioTrack;

struct UltraPrecisionBPMResult {
    double bpm = 0.0;               // Final BPM (±0.1 accuracy)
    double confidence = 0.0;        // 0-1
    std::vector<double> beats;      // Beat positions in seconds
    double offset = 0.0;            // First beat offset
    double bpmVariance = 0.0;       // Variance across passes
    int passCount = 0;              // Number of passes completed
    std::vector<double> passBPMs;   // BPM from each pass
};

class UltraPrecisionBPMService {
public:
    UltraPrecisionBPMService();
    ~UltraPrecisionBPMService();

    UltraPrecisionBPMResult detect(const AudioTrack& track);

    void setMinBPM(double min) { minBPM_ = min; }
    void setMaxBPM(double max) { maxBPM_ = max; }
    void setNumPasses(int passes) { numPasses_ = passes; }
    void setTargetAccuracy(double acc) { targetAccuracy_ = acc; }

private:
    double coarsePass(const float* mono, size_t numSamples, int sampleRate);

    double combFilterPass(const float* mono, size_t numSamples, int sampleRate, double coarseBPM);

    double crossCorrelationPass(const float* mono, size_t numSamples, int sampleRate, double refinedBPM);

    double beatIntervalPass(const std::vector<double>& beats);

    double weightedMedian(const std::vector<double>& bpms, const std::vector<double>& weights);

    // Snap to nearest common BPM fraction (e.g., 128.00 instead of 127.98)
    double snapToGrid(double bpm);

    double minBPM_ = 60.0;
    double maxBPM_ = 200.0;
    int numPasses_ = 4;
    double targetAccuracy_ = 0.1; // ±0.1 BPM
};

} // namespace BeatMate::Core
