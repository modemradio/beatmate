#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace BeatMate::Services::DJSoftware {

struct PlayedTrack {
    std::string filePath;
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    std::string camelotKey;  // Already converted when possible
    double  bpm          = 0.0;
    double  durationSec  = 0.0;
    int     rating       = 0;
    int64_t playedAtUnix = 0;
    std::string source;      // "Serato" / "Rekordbox" / "Traktor" / "VirtualDJ"
};

class DJHistoryReader {
public:
    virtual ~DJHistoryReader() = default;

    // Return the most recent N played tracks (newest first).
    virtual std::vector<PlayedTrack> readRecentHistory(int maxTracks = 200) = 0;

    // Optional - return the track currently playing (if detectable).
    virtual std::optional<PlayedTrack> readNowPlaying() { return std::nullopt; }

    // Human-readable source tag ("Serato", "Rekordbox", ...).
    virtual const char* sourceName() const = 0;
};

} // namespace BeatMate::Services::DJSoftware
