#pragma once

#include <vector>
#include <string>
#include <cmath>

namespace BeatMate::Core {

class AudioTrack;

struct SpectrogramData {
    std::vector<std::vector<float>> magnitude;   // [time][freq] in dB
    std::vector<std::vector<float>> phase;       // [time][freq] in radians
    int numFrames = 0;
    int numBins = 0;
    int fftSize = 0;
    int hopSize = 0;
    int sampleRate = 0;
    double duration = 0.0;

    float binToFreq(int bin) const {
        return static_cast<float>(bin) * sampleRate / fftSize;
    }

    double frameToTime(int frame) const {
        return static_cast<double>(frame) * hopSize / sampleRate;
    }
};

struct MelSpectrogramData {
    std::vector<std::vector<float>> bands;   // [time][melBand] in dB
    int numFrames = 0;
    int numBands = 0;
    int sampleRate = 0;
    double duration = 0.0;
};

class SpectrogramService {
public:
    SpectrogramService();
    ~SpectrogramService();

    SpectrogramData compute(const AudioTrack& track, int fftSize = 2048, int hopSize = 512);

    MelSpectrogramData computeMel(const AudioTrack& track, int numBands = 128,
                                   int fftSize = 2048, int hopSize = 512);

    std::vector<float> getSlice(const SpectrogramData& spec, double timeSeconds);

    std::vector<float> getBandEnergy(const SpectrogramData& spec, float freqLow, float freqHigh);

private:
    static float hzToMel(float hz) { return 2595.0f * std::log10(1.0f + hz / 700.0f); }
    static float melToHz(float mel) { return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f); }

    std::vector<std::vector<float>> createMelFilterbank(int numBands, int fftSize, int sampleRate);
};

} // namespace BeatMate::Core
