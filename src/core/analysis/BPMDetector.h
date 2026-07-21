#pragma once

#include <memory>
#include <string>
#include <vector>

namespace BeatMate::Core {

class AudioTrack;

struct BPMResult {
    double bpm = 0.0;
    double confidence = 0.0; // 0 to 1
    std::vector<double> beats; // Beat positions in seconds
    double offset = 0.0; // First beat offset
};

class BPMDetector {
public:
    BPMDetector();
    ~BPMDetector();

    BPMResult detect(const AudioTrack& track);

    void setMinBPM(double min) { minBPM_ = min; }
    void setMaxBPM(double max) { maxBPM_ = max; }

private:
    std::vector<double> computeOnsetFunction(const AudioTrack& track);

    double estimateTempo(const std::vector<double>& onsetFunction, int sampleRate);

    std::vector<double> trackBeats(const std::vector<double>& onsetFunction,
                                    double tempo, int hopSize, int sampleRate);

    std::vector<double> computeSpectralFlux(const float* mono, size_t numSamples,
                                             int sampleRate, int fftSize, int hopSize);

    double minBPM_ = 60.0;
    double maxBPM_ = 200.0;
};

} // namespace BeatMate::Core
