#pragma once

#include <vector>

namespace BeatMate::Core {

class AudioTrack;

struct DownbeatResult {
    std::vector<double> downbeats;     // Downbeat positions in seconds (bar starts)
    int beatsPerBar = 4;
    double bpm = 0.0;
    float confidence = 0.0f;
    int downbeatPhase = 0;             // Which beat in the bar is the downbeat (0-3)
};

class DownbeatDetectionService {
public:
    DownbeatDetectionService();
    ~DownbeatDetectionService();

    DownbeatResult detect(const AudioTrack& track, const std::vector<double>& beats, double bpm);

    void setBeatsPerBar(int bpb) { beatsPerBar_ = bpb; }

private:
    std::vector<float> computeAccentPattern(const AudioTrack& track,
                                             const std::vector<double>& beats);

    int findDownbeatPhase(const std::vector<float>& accents, int beatsPerBar);

    float scorePhase(const std::vector<float>& accents, int phase, int beatsPerBar);

    int beatsPerBar_ = 4;
};

} // namespace BeatMate::Core
