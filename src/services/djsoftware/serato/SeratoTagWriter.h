#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "../../../models/Track.h"
#include "../../../models/CuePoint.h"

namespace BeatMate::Services::Serato {

/**
 * SeratoTagWriter - Write Serato markers into MP3/FLAC ID3 tags
 *
 * Serato stores cue points in GEOB (General Encapsulated Object) ID3 frames:
 * - "Serato Markers_" (legacy, 14 entries x 22 bytes)
 * - "Serato Markers2" (modern, base64 encoded, variable entries)
 * - "Serato BeatGrid" (beat grid data)
 * - "Serato Autotags" (BPM, auto-gain)
 *
 * Serato Markers2 entry types:
 *   CUE: position (ms), color (RGB), index (0-7), name
 *   LOOP: startpos, endpos, color, locked, name
 *   COLOR: track color (ARGB)
 *   BPMLOCK: boolean
 *
 * Positions in milliseconds.
 * Colors: 3 bytes RGB.
 * Encoding: base64 with Serato's custom padding.
 *
 * Reference: https://github.com/Holzhaus/serato-tags
 */
class SeratoTagWriter {
public:
    struct SeratoCue {
        int index = 0;          // 0-7
        double positionMs = 0;  // milliseconds
        uint8_t r = 0, g = 0, b = 0;
        std::string name;
    };

    struct SeratoLoop {
        int index = 0;
        double startMs = 0, endMs = 0;
        uint8_t r = 0, g = 0, b = 0;
        bool locked = false;
        std::string name;
    };

    // Write Serato markers to an MP3 file's ID3 tags (real GEOB frames).
    bool writeToFile(const std::string& filePath,
                     const std::vector<SeratoCue>& cues,
                     const std::vector<SeratoLoop>& loops = {},
                     float bpm = 0.0f,
                     double firstBeatSec = 0.0);

    // Convert BeatMate cues to Serato format
    static std::vector<SeratoCue> fromBeatMateCues(const std::vector<Models::CuePoint>& cues);

    // Generate Serato Markers2 payload (base64-encoded)
    std::vector<uint8_t> generateMarkers2(const std::vector<SeratoCue>& cues,
                                            const std::vector<SeratoLoop>& loops);

private:
    // Serato base64 encoding (custom variant)
    std::string seratoBase64Encode(const std::vector<uint8_t>& data);
    void writeUint32BE(std::vector<uint8_t>& buf, uint32_t value);
    void writeUint8(std::vector<uint8_t>& buf, uint8_t value);
    void writeString(std::vector<uint8_t>& buf, const std::string& s);
};

} // namespace BeatMate::Services::Serato
