#pragma once

#include <vector>
#include <string>

namespace BeatMate::Core {

class AudioTrack;

struct VocalRegion {
    double startTime = 0.0;
    double endTime = 0.0;
    float confidence = 0.0f;
    float averageEnergy = 0.0f;
    std::string type;           // "vocal", "harmony", "rap", "spoken"
};

struct VocalDetectionResult {
    std::vector<VocalRegion> regions;
    float vocalPercentage = 0.0f;    // % of track with vocals
    bool hasVocals = false;
    float overallConfidence = 0.0f;
    std::vector<float> vocalCurve;   // Per-frame vocal probability (0-1)
    double frameDuration = 0.0;
};

class VocalDetectionService {
public:
    VocalDetectionService();
    ~VocalDetectionService();

    VocalDetectionResult detect(const AudioTrack& track);

    void setThreshold(float thresh) { threshold_ = thresh; }
    void setMinRegionDuration(double sec) { minRegionDuration_ = sec; }

private:
    std::vector<float> computeVocalLikelihood(const AudioTrack& track);

    float computeHNR(const float* data, int numSamples, int sampleRate);

    // Flatness spectrale sur la plage vocale 1-5 kHz
    float computeVocalSpectralFlatness(const std::vector<float>& mag, int sampleRate, int fftSize);

    std::vector<VocalRegion> mergeRegions(const std::vector<VocalRegion>& regions, double gap);

    float threshold_ = 0.5f;
    double minRegionDuration_ = 0.5; // 500ms minimum vocal region
};

} // namespace BeatMate::Core
