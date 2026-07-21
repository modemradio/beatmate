#include "SetExportServiceComplete.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <nlohmann/json.hpp>

namespace BeatMate::Services::Preparation {

ExportResult SetExportServiceComplete::exportSet(
    const std::vector<Models::Track>& tracks, const std::string& filePath, const ExportConfig& config) {

    ExportResult result;
    result.trackCount = static_cast<int>(tracks.size());

    switch (config.format) {
        case ExportFormat::PlainText: result.content = exportToText(tracks, config); break;
        case ExportFormat::JSON:      result.content = exportToJson(tracks, config); break;
        case ExportFormat::M3U:
        case ExportFormat::M3U8:      result.content = exportToM3U(tracks, config); break;
        case ExportFormat::CSV:       result.content = exportToCSV(tracks, config); break;
        case ExportFormat::HTML:      result.content = exportToHTML(tracks, config); break;
    }

    if (!filePath.empty()) {
        result.success = writeFile(filePath, result.content);
        result.filePath = filePath;
        if (!result.success) result.errorMessage = "Failed to write file: " + filePath;
    } else {
        result.success = true;
    }

    spdlog::info("SetExportServiceComplete: Exported {} tracks to {}", result.trackCount,
                 filePath.empty() ? "string" : filePath);
    return result;
}

std::string SetExportServiceComplete::exportToText(const std::vector<Models::Track>& tracks, const ExportConfig& config) {
    std::ostringstream ss;
    if (!config.setName.empty()) ss << "Set: " << config.setName << "\n";
    if (!config.djName.empty()) ss << "DJ: " << config.djName << "\n";
    if (!config.eventName.empty()) ss << "Event: " << config.eventName << "\n";
    if (!config.date.empty()) ss << "Date: " << config.date << "\n";
    ss << "Tracks: " << tracks.size() << "\n";
    ss << std::string(50, '-') << "\n\n";

    for (size_t i = 0; i < tracks.size(); ++i) {
        const auto& t = tracks[i];
        if (config.includeTrackNumbers) ss << std::setw(3) << (i + 1) << ". ";
        ss << t.artist << " - " << t.title;
        if (config.includeBpm && t.bpm > 0) ss << " [" << static_cast<int>(t.bpm) << " BPM]";
        if (config.includeKey) {
            std::string key = t.camelotKey.empty() ? t.key : t.camelotKey;
            if (!key.empty()) ss << " [" << key << "]";
        }
        if (config.includeEnergy && t.energy > 0) ss << " [E:" << std::fixed << std::setprecision(1) << t.energy << "]";
        if (config.includeDuration && t.duration > 0) ss << " (" << formatDuration(t.duration) << ")";
        ss << "\n";
    }

    double totalDur = 0;
    for (const auto& t : tracks) totalDur += t.duration;
    ss << "\n" << std::string(50, '-') << "\n";
    ss << "Total duration: " << formatDuration(totalDur) << "\n";
    return ss.str();
}

std::string SetExportServiceComplete::exportToJson(const std::vector<Models::Track>& tracks, const ExportConfig& config) {
    nlohmann::json j;
    j["setName"] = config.setName;
    j["djName"] = config.djName;
    j["eventName"] = config.eventName;
    j["date"] = config.date;
    j["trackCount"] = tracks.size();

    nlohmann::json tracksJson = nlohmann::json::array();
    for (size_t i = 0; i < tracks.size(); ++i) {
        const auto& t = tracks[i];
        nlohmann::json tj;
        tj["position"] = i + 1;
        tj["title"] = t.title;
        tj["artist"] = t.artist;
        if (config.includeBpm) tj["bpm"] = t.bpm;
        if (config.includeKey) tj["key"] = t.camelotKey.empty() ? t.key : t.camelotKey;
        if (config.includeEnergy) tj["energy"] = t.energy;
        if (config.includeDuration) tj["duration"] = t.duration;
        if (config.includeGenre) tj["genre"] = t.genre;
        tj["filePath"] = t.filePath;
        tracksJson.push_back(tj);
    }
    j["tracks"] = tracksJson;

    double totalDur = 0;
    for (const auto& t : tracks) totalDur += t.duration;
    j["totalDuration"] = totalDur;
    j["totalDurationFormatted"] = formatDuration(totalDur);

    return j.dump(2);
}

std::string SetExportServiceComplete::exportToM3U(const std::vector<Models::Track>& tracks, const ExportConfig& config) {
    std::ostringstream ss;
    ss << "#EXTM3U\n";
    if (!config.setName.empty()) ss << "#PLAYLIST:" << config.setName << "\n";

    for (const auto& t : tracks) {
        int durSec = static_cast<int>(t.duration);
        ss << "#EXTINF:" << durSec << "," << t.artist << " - " << t.title << "\n";
        ss << t.filePath << "\n";
    }
    return ss.str();
}

std::string SetExportServiceComplete::exportToCSV(const std::vector<Models::Track>& tracks, const ExportConfig& config) {
    std::ostringstream ss;
    ss << "Position,Title,Artist";
    if (config.includeBpm) ss << ",BPM";
    if (config.includeKey) ss << ",Key";
    if (config.includeEnergy) ss << ",Energy";
    if (config.includeDuration) ss << ",Duration";
    if (config.includeGenre) ss << ",Genre";
    ss << "\n";

    for (size_t i = 0; i < tracks.size(); ++i) {
        const auto& t = tracks[i];
        ss << (i + 1) << "," << escapeCSV(t.title) << "," << escapeCSV(t.artist);
        if (config.includeBpm) ss << "," << std::fixed << std::setprecision(1) << t.bpm;
        if (config.includeKey) ss << "," << (t.camelotKey.empty() ? t.key : t.camelotKey);
        if (config.includeEnergy) ss << "," << std::fixed << std::setprecision(1) << t.energy;
        if (config.includeDuration) ss << "," << formatDuration(t.duration);
        if (config.includeGenre) ss << "," << escapeCSV(t.genre);
        ss << "\n";
    }
    return ss.str();
}

std::string SetExportServiceComplete::exportToHTML(const std::vector<Models::Track>& tracks, const ExportConfig& config) {
    std::ostringstream ss;
    ss << "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\">\n";
    ss << "<title>" << escapeHTML(config.setName) << "</title>\n";
    ss << "<style>body{font-family:Arial,sans-serif;background:#1a1a2e;color:#e0e0e0;padding:20px;}"
       << "h1{color:#00d9ff;}table{border-collapse:collapse;width:100%;}"
       << "th,td{padding:8px 12px;text-align:left;border-bottom:1px solid #333;}"
       << "th{background:#16213e;color:#00d9ff;}tr:hover{background:#0f3460;}"
       << ".bpm{color:#00d9ff;}.key{color:#4ade80;}.energy{color:#f59e0b;}</style>\n";
    ss << "</head><body>\n";
    if (!config.setName.empty()) ss << "<h1>" << escapeHTML(config.setName) << "</h1>\n";
    if (!config.djName.empty()) ss << "<p>DJ: " << escapeHTML(config.djName) << "</p>\n";
    if (!config.eventName.empty()) ss << "<p>Event: " << escapeHTML(config.eventName) << "</p>\n";
    ss << "<table><tr><th>#</th><th>Title</th><th>Artist</th>";
    if (config.includeBpm) ss << "<th>BPM</th>";
    if (config.includeKey) ss << "<th>Key</th>";
    if (config.includeEnergy) ss << "<th>Energy</th>";
    if (config.includeDuration) ss << "<th>Duration</th>";
    ss << "</tr>\n";

    for (size_t i = 0; i < tracks.size(); ++i) {
        const auto& t = tracks[i];
        ss << "<tr><td>" << (i + 1) << "</td><td>" << escapeHTML(t.title)
           << "</td><td>" << escapeHTML(t.artist) << "</td>";
        if (config.includeBpm) ss << "<td class=\"bpm\">" << static_cast<int>(t.bpm) << "</td>";
        if (config.includeKey) ss << "<td class=\"key\">" << (t.camelotKey.empty() ? t.key : t.camelotKey) << "</td>";
        if (config.includeEnergy) ss << "<td class=\"energy\">" << std::fixed << std::setprecision(1) << t.energy << "</td>";
        if (config.includeDuration) ss << "<td>" << formatDuration(t.duration) << "</td>";
        ss << "</tr>\n";
    }

    double totalDur = 0;
    for (const auto& t : tracks) totalDur += t.duration;
    ss << "</table>\n<p>Total: " << tracks.size() << " tracks, " << formatDuration(totalDur) << "</p>\n";
    ss << "</body></html>";
    return ss.str();
}

bool SetExportServiceComplete::writeFile(const std::string& filePath, const std::string& content) {
    std::ofstream file(filePath);
    if (!file.is_open()) return false;
    file << content;
    file.close();
    return true;
}

std::string SetExportServiceComplete::formatDuration(double seconds) const {
    int m = static_cast<int>(seconds) / 60;
    int s = static_cast<int>(seconds) % 60;
    std::ostringstream ss;
    ss << m << ":" << std::setfill('0') << std::setw(2) << s;
    return ss.str();
}

std::string SetExportServiceComplete::escapeCSV(const std::string& field) const {
    if (field.find(',') != std::string::npos || field.find('"') != std::string::npos) {
        std::string escaped = field;
        size_t pos = 0;
        while ((pos = escaped.find('"', pos)) != std::string::npos) {
            escaped.insert(pos, "\"");
            pos += 2;
        }
        return "\"" + escaped + "\"";
    }
    return field;
}

std::string SetExportServiceComplete::escapeHTML(const std::string& text) const {
    std::string result;
    for (char c : text) {
        switch (c) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            default: result += c;
        }
    }
    return result;
}

} // namespace BeatMate::Services::Preparation
