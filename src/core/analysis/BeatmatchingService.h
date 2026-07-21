#pragma once

#include <string>
#include <vector>

namespace BeatMate::Core {

class AudioTrack;

struct BeatmatchScore {
    float overallScore = 0.0f;       // 0-100
    float tempoCompatibility = 0.0f; // 0-100 (BPM match)
    float keyCompatibility = 0.0f;   // 0-100 (harmonic match)
    float energyCompatibility = 0.0f;// 0-100 (energy level match)
    float rhythmCompatibility = 0.0f;// 0-100 (rhythm pattern match)
    double bpmDifference = 0.0;      // Absolute BPM difference
    int camelotDistance = 0;          // Steps on Camelot wheel
    std::string recommendation;       // "Perfect", "Good", "Risky", "Clash"
    double suggestedPitchShift = 0.0; // Semitones to pitch shift track B
};

class BeatmatchingService {
public:
    BeatmatchingService();
    ~BeatmatchingService();

    BeatmatchScore computeScore(const AudioTrack& trackA, const AudioTrack& trackB);

    BeatmatchScore computeScore(double bpmA, const std::string& keyA, float energyA,
                                 double bpmB, const std::string& keyB, float energyB);

    void setMaxBPMDifference(double diff) { maxBPMDiff_ = diff; }

private:
    float computeTempoScore(double bpmA, double bpmB);
    float computeKeyScore(const std::string& keyA, const std::string& keyB);
    float computeEnergyScore(float energyA, float energyB);
    std::string getRecommendation(float score);

    double maxBPMDiff_ = 8.0; // Max BPM difference for good match
};

} // namespace BeatMate::Core
