#pragma once

#include <vector>
#include <string>

namespace BeatMate::Core {

class AudioTrack;

struct VocalPosition {
    double startTime = 0.0;
    double endTime = 0.0;
    double peakTime = 0.0;         // Time of loudest vocal moment
    float peakEnergy = 0.0f;
    float averagePitch = 0.0f;     // Estimated pitch in Hz
    int barStart = 0;
    int barEnd = 0;
    std::string label;             // "Verse vocal", "Chorus vocal", etc.
};

struct VocalPositionResult {
    std::vector<VocalPosition> positions;
    std::vector<float> pitchCurve;       // Estimated pitch per frame
    std::vector<float> voicedCurve;      // Voiced probability per frame
    double frameDuration = 0.0;
    int totalVocalBars = 0;
    int totalBars = 0;
};

class VocalPositionDetectorService {
public:
    VocalPositionDetectorService();
    ~VocalPositionDetectorService();

    VocalPositionResult detect(const AudioTrack& track, double bpm = 0.0);

    void setMinPitch(float hz) { minPitch_ = hz; }
    void setMaxPitch(float hz) { maxPitch_ = hz; }

private:
    float detectPitch(const float* data, int numSamples, int sampleRate);

    float computeVoicedProbability(const float* data, int numSamples, int sampleRate);

    float minPitch_ = 80.0f;    // Hz
    float maxPitch_ = 1000.0f;  // Hz
};

} // namespace BeatMate::Core
