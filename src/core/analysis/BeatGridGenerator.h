#pragma once

#include <string>
#include <vector>

namespace BeatMate::Models { struct Track; }

namespace BeatMate::Core {

class AudioTrack;

enum class BeatGridMode {
    Manual    = 0,
    Fixed     = 1,
    AI        = 2,
    AIFlex    = 3,
    Rekordbox = 4
};

struct BeatGrid {
    double bpm = 0.0;
    double firstBeatOffset = 0.0;
    std::vector<double> beatPositions;
    std::vector<double> barPositions;
    int beatsPerBar = 4;
};

struct BeatGridResult {
    bool ok = false;
    BeatGrid grid;
    BeatGridMode modeUsed = BeatGridMode::Fixed;
    float confidence = 0.0f;
    bool isVariableTempo = false;
    std::string error;
};

class BeatGridGenerator {
public:
    BeatGridGenerator();
    ~BeatGridGenerator();

    BeatGrid generate(const AudioTrack& track, double bpm, double firstBeat = -1.0);
    BeatGrid generateFromBeats(const std::vector<double>& beats, double bpm);
    BeatGrid generateFromAnlz(const std::string& anlzDatPath);

    BeatGridResult generateForTrack(BeatGridMode mode,
                                    const Models::Track& track,
                                    const std::string& anlzPathHint = {});

private:
    double findFirstBeat(const AudioTrack& track, double bpm);
};

}
