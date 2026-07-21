#pragma once

#include <vector>
#include <string>

namespace BeatMate::Core {

class AudioTrack;

enum class BeatType { Kick, Snare, HiHat, Other };

struct DetectedBeat {
    double position = 0.0;     // seconds
    BeatType type = BeatType::Other;
    float strength = 0.0f;     // 0-1
    float confidence = 0.0f;   // 0-1
};

struct ProBeatDetectionResult {
    std::vector<DetectedBeat> beats;
    double bpm = 0.0;
    int kickCount = 0;
    int snareCount = 0;
    int hihatCount = 0;
    std::string pattern;       // e.g., "K..S..K.S..." four-on-the-floor
};

class ProBeatDetectionService {
public:
    ProBeatDetectionService();
    ~ProBeatDetectionService();

    ProBeatDetectionResult detect(const AudioTrack& track);

    void setKickFreqRange(float low, float high) { kickLow_ = low; kickHigh_ = high; }
    void setSnareFreqRange(float low, float high) { snareLow_ = low; snareHigh_ = high; }
    void setHiHatFreqRange(float low, float high) { hihatLow_ = low; hihatHigh_ = high; }
    void setThreshold(float thresh) { threshold_ = thresh; }

private:
    std::vector<double> computeSubBandOnsets(const float* mono, size_t numSamples,
                                              int sampleRate, float freqLow, float freqHigh);

    BeatType classifyBeat(const float* mono, size_t position, int sampleRate);

    std::string detectPattern(const std::vector<DetectedBeat>& beats, double bpm);

    float kickLow_ = 30.0f, kickHigh_ = 150.0f;
    float snareLow_ = 150.0f, snareHigh_ = 1500.0f;
    float hihatLow_ = 5000.0f, hihatHigh_ = 16000.0f;
    float threshold_ = 0.3f;
};

} // namespace BeatMate::Core
