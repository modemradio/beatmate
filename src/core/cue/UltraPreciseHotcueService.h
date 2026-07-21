#pragma once

#include "HotCueManager.h"
#include <vector>
#include <string>

namespace BeatMate::Core {

class AudioTrack;

struct PreciseCuePoint {
    CuePoint cue;
    double sampleAccuratePosition = 0.0;  // Sub-sample accurate position
    double beatAlignedPosition = 0.0;     // Nearest beat position
    double barAlignedPosition = 0.0;      // Nearest bar position
    float beatProximity = 0.0f;           // How close to nearest beat (0-1, 1=exact)
    int beatNumber = 0;                    // Which beat in the bar (1-4)
    int barNumber = 0;                     // Which bar in the track
};

class UltraPreciseHotcueService {
public:
    UltraPreciseHotcueService();
    ~UltraPreciseHotcueService();

    PreciseCuePoint snapToBeat(double position, const std::vector<double>& beats, double bpm);

    PreciseCuePoint snapToBar(double position, const std::vector<double>& beats, double bpm);

    PreciseCuePoint snapToZeroCrossing(const AudioTrack& track, double position);

    PreciseCuePoint snapToTransient(const AudioTrack& track, double position, double searchMs = 50.0);

    PreciseCuePoint refine(const AudioTrack& track, const CuePoint& cue,
                            const std::vector<double>& beats, double bpm);

    std::vector<PreciseCuePoint> refineAll(const AudioTrack& track,
                                            const std::vector<CuePoint>& cues,
                                            const std::vector<double>& beats, double bpm);

private:
    double findExactZeroCrossing(const float* data, size_t crossingIndex);

    double findOnset(const float* data, size_t startSample, size_t endSample, int sampleRate);
};

} // namespace BeatMate::Core
