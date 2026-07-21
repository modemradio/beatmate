#pragma once

#include <string>
#include <vector>

namespace BeatMate::Services::Export {

class AbletonExporter {
public:
    struct Clip {
        std::string filePath;   // chemin absolu audio
        double      startSec = 0.0;
        double      endSec   = 0.0;
        double      warpBpm  = 0.0;
        std::string name;
    };

    struct Track {
        std::string name;
        std::vector<Clip> clips;
    };

    struct Project {
        double tempo = 120.0;
        int    timeSigNum = 4;
        int    timeSigDen = 4;
        std::vector<Track> tracks;
    };

    bool exportToFile(const Project& project, const std::string& alsPath) const;

private:
    std::string buildXml(const Project& project) const;
};

} // namespace BeatMate::Services::Export
