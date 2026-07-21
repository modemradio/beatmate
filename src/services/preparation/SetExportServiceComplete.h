#pragma once
#include <string>
#include <vector>
#include "../../models/Track.h"

namespace BeatMate::Services::Preparation {

enum class ExportFormat { PlainText, JSON, M3U, M3U8, CSV, HTML };

struct ExportConfig {
    ExportFormat format = ExportFormat::PlainText;
    bool includeTrackNumbers = true;
    bool includeBpm = true;
    bool includeKey = true;
    bool includeEnergy = true;
    bool includeDuration = true;
    bool includeGenre = false;
    bool includeTransitionNotes = false;
    std::string setName;
    std::string djName;
    std::string eventName;
    std::string date;
};

struct ExportResult {
    bool success = false;
    std::string filePath;
    std::string content;
    std::string errorMessage;
    int trackCount = 0;
};

class SetExportServiceComplete {
public:
    SetExportServiceComplete() = default;

    ExportResult exportSet(const std::vector<Models::Track>& tracks, const std::string& filePath, const ExportConfig& config);
    std::string exportToText(const std::vector<Models::Track>& tracks, const ExportConfig& config);
    std::string exportToJson(const std::vector<Models::Track>& tracks, const ExportConfig& config);
    std::string exportToM3U(const std::vector<Models::Track>& tracks, const ExportConfig& config);
    std::string exportToCSV(const std::vector<Models::Track>& tracks, const ExportConfig& config);
    std::string exportToHTML(const std::vector<Models::Track>& tracks, const ExportConfig& config);

private:
    bool writeFile(const std::string& filePath, const std::string& content);
    std::string formatDuration(double seconds) const;
    std::string escapeCSV(const std::string& field) const;
    std::string escapeHTML(const std::string& text) const;
};

} // namespace BeatMate::Services::Preparation
