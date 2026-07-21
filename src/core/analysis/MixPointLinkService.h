#pragma once

#include <vector>
#include <string>

namespace BeatMate::Core {

class AudioTrack;

struct MixPoint {
    double outPoint = 0.0;       // Point to start fading out track A
    double inPoint = 0.0;        // Point to start fading in track B
    double mixDuration = 0.0;    // Recommended mix duration in seconds
    int mixBars = 0;             // Mix duration in bars
    float score = 0.0f;          // Quality score 0-100
    std::string type;            // "energy", "breakdown", "outro-intro", "drop"
    std::string description;
};

struct MixPointResult {
    std::vector<MixPoint> candidates;
    MixPoint bestMatch;
    double bpmA = 0.0;
    double bpmB = 0.0;
    float overallScore = 0.0f;
};

class MixPointLinkService {
public:
    MixPointLinkService();
    ~MixPointLinkService();

    MixPointResult findMixPoints(const AudioTrack& trackA, const AudioTrack& trackB);

    // Find mix point with pre-analyzed data
    MixPointResult findMixPoints(const AudioTrack& trackA, double bpmA,
                                   const std::vector<float>& energyA,
                                   const AudioTrack& trackB, double bpmB,
                                   const std::vector<float>& energyB);

    void setPreferredMixBars(int bars) { preferredMixBars_ = bars; }
    void setMinScore(float score) { minScore_ = score; }

private:
    // Find energy-drop points in track A (good mix-out candidates)
    std::vector<double> findEnergyDropPoints(const std::vector<float>& energy, double segDuration);

    // Find energy-rise points in track B (good mix-in candidates)
    std::vector<double> findEnergyRisePoints(const std::vector<float>& energy, double segDuration);

    // Score a mix point pair
    float scoreMixPoint(const std::vector<float>& energyA, double outPoint,
                         const std::vector<float>& energyB, double inPoint,
                         double segDuration, double bpmA, double bpmB);

    int preferredMixBars_ = 16;
    float minScore_ = 30.0f;
};

} // namespace BeatMate::Core
