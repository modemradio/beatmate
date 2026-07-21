#pragma once

#include "DJHistoryReader.h"

#include <juce_core/juce_core.h>
#include <cstdint>
#include <string>
#include <vector>

namespace BeatMate::Services::DJSoftware {

struct RekordboxCue {
    int         kind      = 0;     // 0 = memory cue, 1..8 = hot cue A..H
    double      startSec  = 0.0;
    double      endSec    = -1.0;  // >0 for a loop, -1 otherwise
    juce::String comment;
    uint32_t    color     = 0;     // 0x00RRGGBB
};

struct RekordboxPlaylist {
    int64_t               id            = 0;
    int64_t               parentId      = 0;   // 0 = root
    juce::String          name;
    int                   attribute     = 0;   // 0 = playlist, 1 = folder, 4 = smart
    int                   trackCount    = 0;
    std::vector<int64_t>  trackIds;             // from djmdSongPlaylist, ordered
};

struct RekordboxHistorySession {
    int64_t               id            = 0;
    juce::String          name;
    juce::String          dateCreated;          // ISO-8601 string as stored
    std::vector<int64_t>  trackIds;
};

struct RekordboxDbTrack {
    int64_t       id               = 0;
    juce::String  title;
    juce::String  artist;
    juce::String  album;
    juce::String  genre;
    juce::String  label;
    juce::String  keyName;          // "Am", "8A", ...
    juce::String  camelot;          // ScaleName + KeyName when available
    juce::String  folderPath;       // concatenated with fileName
    juce::String  fileName;
    juce::String  imagePath;
    juce::String  analysisDataPath; // relative path to ANLZ / EXT
    double        bpm              = 0.0;
    double        lengthSec        = 0.0;
    int           rating           = 0;
    int           year             = 0;
    juce::String  dateAdded;        // ISO-8601
};

class RekordboxHistoryReader final : public DJHistoryReader {
public:
    std::vector<PlayedTrack> readRecentHistory(int maxTracks = 500) override;
    std::optional<PlayedTrack> readNowPlaying() override;
    const char* sourceName() const override { return "Rekordbox"; }

    std::vector<RekordboxDbTrack> getAllTracks();

    std::vector<RekordboxCue> getCuesForTrack(int64_t contentId);

    std::vector<RekordboxCue> getCuesForNowPlaying();

    std::vector<RekordboxPlaylist> getPlaylistTree();

    std::vector<RekordboxPlaylist> getAllPlaylistsFlat();

    std::vector<RekordboxHistorySession> getHistorySessions();

    void logStartupSummary();

    bool isRekordboxPresent();
};

} // namespace BeatMate::Services::DJSoftware
