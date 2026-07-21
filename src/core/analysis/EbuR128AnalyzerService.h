#pragma once

#include <vector>

namespace BeatMate::Core {

class AudioTrack;

struct EbuR128Result {
    float integratedLUFS = -70.0f;     // Program loudness (ITU-R BS.1770-4)
    float shortTermLUFS = -70.0f;      // 3-second window max
    float momentaryLUFS = -70.0f;      // 400ms window max
    float loudnessRange = 0.0f;        // LRA (LU)
    float truePeakdBTP = -100.0f;      // True peak (dB TP)
    float maxShortTermLUFS = -70.0f;   // Max short-term loudness
    float maxMomentaryLUFS = -70.0f;   // Max momentary loudness

    std::vector<float> momentaryCurve;     // Momentary loudness over time
    std::vector<float> shortTermCurve;     // Short-term loudness over time
    float momentaryInterval = 0.1f;        // seconds between samples

    // EBU R 128 gate thresholds
    float absoluteGateThreshold = -70.0f;  // dB
    float relativeGateThreshold = 0.0f;    // dB (computed)
};

class EbuR128AnalyzerService {
public:
    EbuR128AnalyzerService();
    ~EbuR128AnalyzerService();

    EbuR128Result analyze(const AudioTrack& track);

    float getMomentaryLoudness(const AudioTrack& track, double timeSeconds);
    float getShortTermLoudness(const AudioTrack& track, double timeSeconds);

private:
    // K-weighting filter (ITU-R BS.1770-4 compliant)
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;

        double process(double x) {
            double y = b0 * x + b1 * z1 + b2 * z2 - a1 * z1 - a2 * z2;
            double w = x - a1 * z1 - a2 * z2;
            y = b0 * w + b1 * z1 + b2 * z2;
            z2 = z1;
            z1 = w;
            return y;
        }

        void reset() { z1 = z2 = 0; }
    };

    void initKWeighting(int sampleRate);

    void applyKWeighting(const float* input, float* output, int numSamples, int channel);

    double computeBlockMeanSquare(const float* data, int numSamples, int channels);

    float computeGatedLoudness(const std::vector<double>& blockLoudness);

    float computeLRA(const std::vector<double>& shortTermLoudness);

    float computeTruePeak(const float* data, size_t numSamples, int channels);

    Biquad preFilter_[2];   // Per channel (up to stereo)
    Biquad rlbFilter_[2];

    int sampleRate_ = 44100;
};

} // namespace BeatMate::Core
