#pragma once

#include <vector>
#include <string>

namespace BeatMate::Core {

class AudioTrack;

struct EnergyBand {
    float energy = 0.0f;
    float peak = 0.0f;
    float rms = 0.0f;
    float crest = 0.0f;
};

struct RealEnergyResult {
    int overallEnergy = 5;
    float rmsGlobal = 0.0f;
    float spectralCentroid = 0.0f;
    float spectralSpread = 0.0f;
    float spectralFlatness = 0.0f;
    float spectralRolloff = 0.0f;
    float lufs = -70.0f;
    float dynamicRange = 0.0f;

    EnergyBand subBass;
    EnergyBand bass;
    EnergyBand lowMid;
    EnergyBand mid;
    EnergyBand highMid;
    EnergyBand presence;
    EnergyBand brilliance;

    std::vector<float> energyCurve;
    std::vector<float> centroidCurve;
    std::vector<float> loudnessCurve;
    double segmentDuration = 0.5;
};

class RealEnergyAnalysisService {
public:
    RealEnergyAnalysisService();
    ~RealEnergyAnalysisService();

    RealEnergyResult analyze(const AudioTrack& track, double segmentDuration = 0.5);

private:
    float computeRMS(const float* data, size_t numSamples);
    float computeSpectralCentroid(const std::vector<float>& mag, int sampleRate, int fftSize);
    float computeSpectralSpread(const std::vector<float>& mag, float centroid, int sampleRate, int fftSize);
    float computeSpectralFlatness(const std::vector<float>& mag);
    float computeSpectralRolloff(const std::vector<float>& mag, int sampleRate, int fftSize, float percent = 0.85f);
    EnergyBand computeBandEnergy(const std::vector<float>& mag, int sampleRate, int fftSize,
                                  float freqLow, float freqHigh);
};

}
