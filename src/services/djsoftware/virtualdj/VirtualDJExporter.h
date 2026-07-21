#pragma once
#include <string>
#include <vector>
#include "../../../models/Track.h"
#include "../../../models/CuePoint.h"

namespace BeatMate::Services::VirtualDJ {

class VirtualDJExporter {
public:
    struct ExportTrack {
        std::string filePath, title, artist, album, genre, comment, key;
        float bpm = 0.0f;
        double firstBeatSec = -1.0; // grid anchor; <0 = unknown
        double duration = 0.0;
        struct Poi {
            std::string type = "cue";   // cue, loop, automix
            double position = 0.0;      // seconds
            double length = 0.0;        // seconds (loops)
            std::string name;
            int number = 0;
        };
        std::vector<Poi> pois;
    };

    void addTrack(const ExportTrack& t);
    bool exportToFile(const std::string& path);
    bool exportToDatabase(const std::string& vdjDatabaseXmlPath);
    static ExportTrack fromBeatMateTrack(const Models::Track& t, const std::vector<Models::CuePoint>& c);
private:
    std::string escapeXml(const std::string& s);
    std::string buildSongXml(const ExportTrack& t);
    std::vector<ExportTrack> tracks_;
};

} // namespace BeatMate::Services::VirtualDJ
