#pragma once

#include <cstdint>
#include <vector>

namespace BeatMate::Core {

class AudioTrack;

struct TempoPoint {
    double timeSec = 0.0;
    double bpm = 0.0;
};

struct BeatGridCore {
    double bpm = 0.0;
    double confidence = 0.0;
    std::vector<double> beats;
    std::vector<double> bars;
    std::vector<TempoPoint> tempoMap;
    int beatsPerBarNum = 4;
    int beatsPerBarDen = 4;
    double firstDownbeatSec = 0.0;
    bool variableTempo = false;
    bool ok = false;
};

struct BeatEngineOptions {
    double minBPM = 70.0;
    double maxBPM = 180.0;
    bool allowVariableTempo = true;
    bool fastMode = false;
    double maxSecondsToAnalyze = 0.0;
};

class BeatEngine {
public:
    BeatEngine();
    ~BeatEngine();

    BeatGridCore analyze(const float* mono, std::int64_t numSamples, double sampleRate,
                         const BeatEngineOptions& options) const;

    BeatGridCore analyze(const AudioTrack& track, const BeatEngineOptions& options) const;

    BeatGridCore analyzeMultiChannel(const float* const* channels, int numChannels,
                                     std::int64_t numFrames, double sampleRate,
                                     const BeatEngineOptions& options) const;
};

} // namespace BeatMate::Core
