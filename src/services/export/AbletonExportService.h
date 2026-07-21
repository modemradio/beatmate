#pragma once
#include <string>
#include <vector>
#include "../../models/Track.h"

namespace BeatMate::Services::Export {

struct AbletonClip {
    std::string name;
    std::string filePath;
    double startBeat = 0.0;
    double lengthBeats = 0.0;
    double bpm = 0.0;
    double sampleRate = 44100.0;
    int64_t sampleLength = 0;
    double warpStartMarker = 0.0;
    double warpEndMarker = 0.0;
    float gain = 1.0f;
    bool isWarped = true;
    int color = 0;
};

struct AbletonTrackInfo {
    std::string name;
    float volume = 1.0f;
    float pan = 0.0f;
    bool isMuted = false;
    bool isSoloed = false;
    int color = 0;
    std::vector<AbletonClip> clips;
};

struct AbletonProjectOptions {
    double masterBpm = 0.0;
    int timeSignatureNum = 4;
    int timeSignatureDen = 4;
    double sampleRate = 44100.0;
    bool createReturnTracks = true;
    bool addMasterLimiter = false;
    std::string projectName = "BeatMate Export";
};

class AbletonExportService {
public:
    AbletonExportService() = default;
    ~AbletonExportService() = default;

    bool exportALS(const std::string& outputPath,
                    const std::vector<AbletonTrackInfo>& tracks,
                    const AbletonProjectOptions& options);

    bool exportFromDJSet(const std::string& outputPath,
                          const std::vector<Models::Track>& djTracks,
                          const std::vector<double>& startTimes,
                          double masterBpm);

    bool exportStemsProject(const std::string& outputPath,
                              const std::string& trackTitle,
                              const std::vector<std::string>& stemPaths,
                              const std::vector<std::string>& stemNames,
                              double bpm);

    static AbletonClip createClip(const Models::Track& track, double startBeat, double fallbackBpm);

private:
    std::string generateALSXml(const std::vector<AbletonTrackInfo>& tracks,
                                 const AbletonProjectOptions& options) const;
    std::string escapeXml(const std::string& s) const;
    bool compressToGzip(const std::string& xmlContent, const std::string& outputPath) const;
};

}
