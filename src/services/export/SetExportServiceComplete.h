#pragma once
#include <string>
#include <vector>
#include "../../models/Track.h"
#include "../../models/Playlist.h"

namespace BeatMate::Services::Export {

struct SetTrackEntry {
    int position = 0;
    std::string title;
    std::string artist;
    double bpm = 0.0;
    std::string key;
    double startTime = 0.0;
    double endTime = 0.0;
    double transitionLength = 0.0;
    std::string transitionType;
    float energy = 0.0f;
    std::string filePath;
};

struct SetExportOptions {
    bool exportText = true;
    bool exportJson = true;
    bool exportM3U = true;
    bool exportPdf = true;
    bool exportCue = true;
    bool includeTimestamps = true;
    bool includeBpmKey = true;
    bool includeTransitions = true;
    std::string djName;
    std::string venueName;
    std::string eventName;
    std::string date;
    std::string notes;
};

struct SetExportResult {
    bool success = false;
    std::string textPath;
    std::string jsonPath;
    std::string m3uPath;
    std::string pdfPath;
    std::string cuePath;
    std::vector<std::string> errors;
};

class SetExportServiceComplete {
public:
    SetExportServiceComplete() = default;
    ~SetExportServiceComplete() = default;

    SetExportResult exportSet(const std::string& outputDir,
                               const std::string& setName,
                               const std::vector<SetTrackEntry>& entries,
                               const SetExportOptions& options);

    bool exportAsText(const std::string& outputPath, const std::vector<SetTrackEntry>& entries,
                       const SetExportOptions& options);
    bool exportAsJson(const std::string& outputPath, const std::vector<SetTrackEntry>& entries,
                       const SetExportOptions& options);
    bool exportAsM3U(const std::string& outputPath, const std::vector<SetTrackEntry>& entries);
    bool exportAsCueSheet(const std::string& outputPath, const std::string& audioFile,
                           const std::vector<SetTrackEntry>& entries, const SetExportOptions& options);

    static std::vector<SetTrackEntry> buildFromTracks(const std::vector<Models::Track>& tracks,
                                                       const std::vector<double>& startTimes);

private:
    std::string formatTime(double seconds) const;
};

} // namespace BeatMate::Services::Export
