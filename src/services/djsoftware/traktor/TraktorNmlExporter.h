#pragma once

#include <string>
#include <vector>
#include "../../../models/Track.h"
#include "../../../models/CuePoint.h"

namespace BeatMate::Services::Traktor {

// NML: CUE_V2 types 0=Cue 1=FadeIn 2=FadeOut 3=Load 4=Grid 5=Loop, positions in ms, MUSICAL_KEY 0-23.
class TraktorNmlExporter {
public:
    struct ExportTrack {
        std::string filePath;
        std::string title, artist, album, genre, comment, key;
        float bpm = 0.0f;
        double firstBeatSec = -1.0; // grid anchor; <0 = unknown (AutoGrid anchors at 0)
        double duration = 0.0;
        struct Cue {
            int type = 0;       // 0=Cue, 5=Loop
            double startMs = 0;
            double lengthMs = 0;
            std::string name;
            int hotcue = -1;    // 0-7, -1 = not a hotcue
        };
        std::vector<Cue> cues;
    };

    void addTrack(const ExportTrack& track);
    bool exportToFile(const std::string& path);
    std::string generateNml();

    static ExportTrack fromBeatMateTrack(const Models::Track& t, const std::vector<Models::CuePoint>& cues);

private:
    std::string escapeXml(const std::string& s);
    int keyToCamelotValue(const std::string& key);
    std::vector<ExportTrack> tracks_;
};

} // namespace BeatMate::Services::Traktor
