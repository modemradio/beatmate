#pragma once

#include <vector>

namespace BeatMate::Core {

class AudioTrack;

struct LoudnessResult {
    float integratedLUFS = -70.0f;
    float shortTermLUFS = -70.0f;
    float momentaryLUFS = -70.0f;
    float loudnessRange = 0.0f;    // LRA in LU
    float truePeakdBTP = -100.0f;
};

class LoudnessAnalyzer {
public:
    LoudnessAnalyzer();
    ~LoudnessAnalyzer();

    LoudnessResult analyze(const AudioTrack& track);

private:
    struct KWeightFilter {
        double a1, a2, b0, b1, b2;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

        double process(double x) {
            double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }

        void reset() { x1 = x2 = y1 = y2 = 0; }
    };

    void initKWeighting(int sampleRate);

    KWeightFilter preFilter_;  // High shelf
    KWeightFilter rlbFilter_;  // Revised low-frequency B-curve

    float computeBlockLoudness(const float* data, int numSamples, int channels);
};

} // namespace BeatMate::Core
