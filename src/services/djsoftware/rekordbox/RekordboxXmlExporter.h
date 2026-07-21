#pragma once

#include <string>
#include <vector>

#include "../../../models/Track.h"
#include "../../../models/CuePoint.h"
#include "../../../models/Playlist.h"

namespace BeatMate::Services::Rekordbox {

// XML DJ_PLAYLISTS compatible Rekordbox 5/6/7 — spec : https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/exports.html
class RekordboxXmlExporter {
public:
    RekordboxXmlExporter() = default;

    struct ExportTrack {
        int64_t trackId = 0;
        std::string filePath;
        std::string title;
        std::string artist;
        std::string album;
        std::string genre;
        std::string comment;
        std::string key;            // Camelot or musical key
        float bpm = 0.0f;
        double firstBeatSec = -1.0; // grid anchor; <0 = unknown (TEMPO Inizio falls back to 0)
        int rating = 0;             // 0-255 (Rekordbox uses 0-255, not 0-5)
        double duration = 0.0;      // seconds
        int sampleRate = 44100;
        int bitRate = 320;          // kbps
        int year = 0;
        std::string dateAdded;
        std::string color;          // hex color #RRGGBB
        std::string remixer;
        std::string label;
        std::string grouping;       // Rekordbox "Grouping" free-text field

        struct CuePointExport {
            int number = 0;         // 0-7 for hot cues
            int type = 0;           // 0=cue, 4=loop
            double startMs = 0.0;   // milliseconds
            double endMs = -1.0;    // -1 if not a loop
            std::string name;
            uint8_t red = 0, green = 0, blue = 0;
        };
        std::vector<CuePointExport> cuePoints;
    };

    struct ExportPlaylist {
        std::string name;
        std::vector<int64_t> trackIds;
    };

    void addTrack(const ExportTrack& track);
    void addPlaylist(const ExportPlaylist& playlist);

    bool exportToFile(const std::string& outputPath);

    std::string generateXml();

    static ExportTrack fromBeatMateTrack(const Models::Track& track,
                                           const std::vector<Models::CuePoint>& cues);

private:
    std::string escapeXml(const std::string& s);
    std::string colorToRGB(const std::string& hexColor, const std::string& attr);
    std::string bpmToKey(const std::string& key);

    std::vector<ExportTrack> tracks_;
    std::vector<ExportPlaylist> playlists_;
};

} // namespace BeatMate::Services::Rekordbox
