#pragma once

#include <string>
#include <vector>

#include "../../models/Track.h"

namespace BeatMate::Services::Export {

struct CueIndex {
    int trackNumber = 1;
    std::string title;
    std::string performer;
    int minutes = 0;
    int seconds = 0;
    int frames = 0;    // 1/75th of a second (CD standard)
    std::string isrc;
    std::string songwriter;
};

struct CueSheetMetadata {
    std::string title;         // TITLE
    std::string performer;     // PERFORMER
    std::string songwriter;    // SONGWRITER
    std::string catalog;       // CATALOG (UPC/EAN)
    std::string genre;         // REM GENRE
    std::string date;          // REM DATE
    std::string comment;       // REM COMMENT
    std::string discId;        // CDDB DISC ID
    std::string fileName;      // FILE "name" WAVE
    std::string fileType;      // WAVE, MP3, AIFF, BINARY
};

class CueSheetExportService {
public:
    CueSheetExportService() = default;
    ~CueSheetExportService() = default;

    bool exportCueSheet(const std::string& outputPath,
                         const CueSheetMetadata& metadata,
                         const std::vector<CueIndex>& indices);

    bool exportFromTracks(const std::string& outputPath,
                           const std::string& audioFileName,
                           const std::vector<Models::Track>& tracks,
                           const std::vector<double>& startTimesSeconds);

    bool exportFromMixSession(const std::string& outputPath,
                               const std::string& audioFileName,
                               const std::string& djName,
                               const std::string& mixTitle,
                               const std::vector<Models::Track>& tracks,
                               const std::vector<double>& transitionTimesSeconds);

    bool parseCueSheet(const std::string& filePath,
                        CueSheetMetadata& outMetadata,
                        std::vector<CueIndex>& outIndices);

    static CueIndex timeToIndex(int trackNumber, double seconds,
                                 const std::string& title = "",
                                 const std::string& performer = "");
    static double indexToSeconds(const CueIndex& index);
    static std::string formatTime(int minutes, int seconds, int frames);

private:
    std::string escapeString(const std::string& s) const;
    std::string generateCueContent(const CueSheetMetadata& metadata,
                                    const std::vector<CueIndex>& indices) const;
};

} // namespace BeatMate::Services::Export
