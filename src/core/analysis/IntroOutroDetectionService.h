#pragma once

#include <vector>
#include <string>

namespace BeatMate::Core {

class AudioTrack;

struct IntroOutroResult {
    double introStart = 0.0;
    double introEnd = 0.0;
    int introBars = 0;
    float introEnergy = 0.0f;
    bool hasVocalIntro = false;
    std::string introType;        // "silence", "beat", "buildup", "ambient"

    double outroStart = 0.0;
    double outroEnd = 0.0;
    int outroBars = 0;
    float outroEnergy = 0.0f;
    bool hasVocalOutro = false;
    std::string outroType;

    double mixInPoint = 0.0;
    double mixOutPoint = 0.0;
    float confidence = 0.0f;
};

class IntroOutroDetectionService {
public:
    IntroOutroDetectionService();
    ~IntroOutroDetectionService();

    IntroOutroResult detect(const AudioTrack& track, double bpm = 0.0);

    void setEnergyThreshold(float thresh) { energyThreshold_ = thresh; }
    void setMaxIntroBars(int bars) { maxIntroBars_ = bars; }

private:
    double findEnergyRise(const std::vector<float>& energyProfile, double barDuration);

    double findEnergyDrop(const std::vector<float>& energyProfile, double barDuration);

    std::string classifyType(const AudioTrack& track, double start, double end);

    float energyThreshold_ = 0.3f;
    int maxIntroBars_ = 32;
};

} // namespace BeatMate::Core
