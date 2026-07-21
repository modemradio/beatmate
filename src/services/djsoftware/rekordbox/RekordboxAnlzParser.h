#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace BeatMate::Services::Rekordbox {

// RekordboxAnlzParser — parses Pioneer's ANLZ{xxxx}.DAT/.EXT files found
struct AnlzBeat {
    uint8_t  beatNumber = 0;   // 1..4
    double   bpm        = 0.0; // decoded to float BPM
    double   timeSec    = 0.0; // decoded from ms
};

struct AnlzCue {
    int          hotCueIndex = -1;     // -1 = memory cue, 0..7 = hot cue A..H
    double       positionSec = 0.0;
    double       loopEndSec  = 0.0;    // 0 if not a loop
    uint32_t     rgbColor    = 0;      // 0x00RRGGBB
    std::string  comment;
};

// RGB sample at one waveform column. BM (bass/mid/high) 3-byte colour code
struct AnlzWavColumn {
    uint8_t heightBass = 0;  // 0..31
    uint8_t heightMid  = 0;  // 0..31
    uint8_t heightHigh = 0;  // 0..31
};

struct AnlzWaveform {
    std::vector<AnlzWavColumn> columns;
};

class RekordboxAnlzParser {
public:
    // Parse the entire ANLZ file (caller should pass the .DAT path).
    static bool parseFile(const std::string& path,
                          std::vector<AnlzBeat>*      outBeats,
                          std::vector<AnlzCue>*       outCues,
                          AnlzWaveform*               outWaveform);

    // Helper: read only the beats (PQTZ). Returns empty on failure.
    static std::vector<AnlzBeat> readBeats(const std::string& path);
};

} // namespace BeatMate::Services::Rekordbox
