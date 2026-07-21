#include "PdfExportService.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <map>

namespace BeatMate::Services::Export {

bool PdfExportService::exportSetlistPdf(const std::string& outputPath,
                                          const std::vector<Models::Track>& tracks,
                                          const std::vector<double>& startTimes,
                                          const SetlistPdfOptions& options) {
    std::vector<std::string> lines;

    if (!options.djName.empty()) lines.push_back("DJ: " + options.djName);
    if (!options.eventName.empty()) lines.push_back("Event: " + options.eventName);
    if (!options.venueName.empty()) lines.push_back("Venue: " + options.venueName);
    if (!options.date.empty()) lines.push_back("Date: " + options.date);
    lines.push_back("Tracks: " + std::to_string(tracks.size()));
    lines.push_back("");

    for (size_t i = 0; i < tracks.size(); ++i) {
        std::ostringstream oss;
        if (options.showTrackNumbers) {
            oss << std::setw(3) << (i + 1) << ". ";
        }
        if (options.showTimestamps && i < startTimes.size()) {
            oss << "[" << formatTime(startTimes[i]) << "] ";
        }
        oss << tracks[i].artist << " - " << tracks[i].title;
        if (options.showBpmKey) {
            oss << "  |  " << std::fixed << std::setprecision(1) << tracks[i].bpm << " BPM";
            if (!tracks[i].key.empty()) oss << " / " << tracks[i].key;
        }
        if (options.showGenre && !tracks[i].genre.empty()) {
            oss << "  |  " << tracks[i].genre;
        }
        if (options.showEnergy && tracks[i].energy > 0) {
            oss << "  |  E:" << std::setprecision(0) << tracks[i].energy;
        }
        lines.push_back(oss.str());
    }

    std::string title = options.eventName.empty() ? "Setlist" : options.eventName;
    std::string content = generatePdfContent(title, lines, options);

    std::ofstream ofs(outputPath, std::ios::binary);
    if (!ofs.is_open()) {
        spdlog::error("PdfExportService: Failed to create {}", outputPath);
        return false;
    }
    ofs << content;
    spdlog::info("PdfExportService: Exported setlist PDF ({} tracks) to {}", tracks.size(), outputPath);
    return true;
}

bool PdfExportService::exportAnalysisReport(const std::string& outputPath,
                                              const std::vector<Models::Track>& tracks,
                                              const AnalysisReportOptions& options) {
    std::vector<std::string> lines;
    lines.push_back("Analysis Report - " + std::to_string(tracks.size()) + " tracks");
    lines.push_back("");

    if (options.showStatistics) {
        double totalDuration = 0;
        double avgBpm = 0;
        int analyzedCount = 0;
        std::map<std::string, int> genreCounts;
        std::map<std::string, int> keyCounts;

        for (const auto& t : tracks) {
            totalDuration += t.duration;
            if (t.bpm > 0) { avgBpm += t.bpm; analyzedCount++; }
            if (!t.genre.empty()) genreCounts[t.genre]++;
            if (!t.key.empty()) keyCounts[t.key]++;
        }
        if (analyzedCount > 0) avgBpm /= analyzedCount;

        lines.push_back("=== STATISTICS ===");
        lines.push_back("Total tracks: " + std::to_string(tracks.size()));
        lines.push_back("Total duration: " + formatTime(totalDuration));
        lines.push_back("Average BPM: " + std::to_string(static_cast<int>(avgBpm)));
        lines.push_back("Analyzed: " + std::to_string(analyzedCount) + "/" + std::to_string(tracks.size()));
        lines.push_back("");

        if (options.showGenreBreakdown && !genreCounts.empty()) {
            lines.push_back("=== GENRE BREAKDOWN ===");
            std::vector<std::pair<std::string, int>> sorted(genreCounts.begin(), genreCounts.end());
            std::sort(sorted.begin(), sorted.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });
            for (const auto& [genre, count] : sorted) {
                int pct = static_cast<int>(100.0 * count / tracks.size());
                lines.push_back("  " + genre + ": " + std::to_string(count) + " (" + std::to_string(pct) + "%)");
            }
            lines.push_back("");
        }

        if (options.showKeyDistribution && !keyCounts.empty()) {
            lines.push_back("=== KEY DISTRIBUTION ===");
            std::vector<std::pair<std::string, int>> sorted(keyCounts.begin(), keyCounts.end());
            std::sort(sorted.begin(), sorted.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });
            for (const auto& [key, count] : sorted) {
                lines.push_back("  " + key + ": " + std::to_string(count));
            }
            lines.push_back("");
        }
    }

    lines.push_back("=== TRACK DETAILS ===");
    for (size_t i = 0; i < tracks.size(); ++i) {
        std::ostringstream oss;
        oss << (i + 1) << ". " << tracks[i].artist << " - " << tracks[i].title;
        oss << "  [" << std::fixed << std::setprecision(1) << tracks[i].bpm << " BPM";
        if (!tracks[i].key.empty()) oss << ", " << tracks[i].key;
        oss << ", " << formatTime(tracks[i].duration) << "]";
        lines.push_back(oss.str());
    }

    std::string content = generatePdfContent("Analysis Report", lines, options);
    std::ofstream ofs(outputPath, std::ios::binary);
    if (!ofs.is_open()) return false;
    ofs << content;
    spdlog::info("PdfExportService: Exported analysis report to {}", outputPath);
    return true;
}

bool PdfExportService::exportPlaylistPdf(const std::string& outputPath,
                                           const Models::Playlist& playlist,
                                           const std::vector<Models::Track>& tracks,
                                           const PdfPageOptions& options) {
    std::vector<std::string> lines;
    if (!playlist.description.empty()) {
        lines.push_back(playlist.description);
        lines.push_back("");
    }
    lines.push_back("Tracks: " + std::to_string(tracks.size()));
    lines.push_back("");

    for (size_t i = 0; i < tracks.size(); ++i) {
        std::ostringstream oss;
        oss << std::setw(3) << (i + 1) << ". " << tracks[i].artist << " - " << tracks[i].title;
        if (tracks[i].bpm > 0) {
            oss << "  (" << std::fixed << std::setprecision(0) << tracks[i].bpm << " BPM)";
        }
        lines.push_back(oss.str());
    }

    std::string content = generatePdfContent(playlist.name, lines, options);
    std::ofstream ofs(outputPath, std::ios::binary);
    if (!ofs.is_open()) return false;
    ofs << content;
    return true;
}

bool PdfExportService::exportTrackCard(const std::string& outputPath,
                                         const Models::Track& track,
                                         const PdfPageOptions& options) {
    std::vector<std::string> lines;
    lines.push_back("Artist: " + track.artist);
    lines.push_back("Title: " + track.title);
    if (!track.album.empty()) lines.push_back("Album: " + track.album);
    if (!track.genre.empty()) lines.push_back("Genre: " + track.genre);
    if (track.year > 0) lines.push_back("Year: " + std::to_string(track.year));
    lines.push_back("");
    lines.push_back("Duration: " + formatTime(track.duration));
    if (track.bpm > 0) lines.push_back("BPM: " + std::to_string(static_cast<int>(track.bpm)));
    if (!track.key.empty()) lines.push_back("Key: " + track.key);
    if (track.energy > 0) lines.push_back("Energy: " + std::to_string(static_cast<int>(track.energy)) + "/10");
    lines.push_back("");
    lines.push_back("Sample Rate: " + std::to_string(track.sampleRate) + " Hz");
    lines.push_back("Bit Rate: " + std::to_string(track.bitRate) + " kbps");
    lines.push_back("Format: " + track.fileFormat);

    std::string content = generatePdfContent(track.artist + " - " + track.title, lines, options);
    std::ofstream ofs(outputPath, std::ios::binary);
    if (!ofs.is_open()) return false;
    ofs << content;
    return true;
}

std::string PdfExportService::generatePdfContent(const std::string& title,
                                                    const std::vector<std::string>& lines,
                                                    const PdfPageOptions& options) const {
    std::ostringstream pdf;

    double pageW = options.landscape ? options.pageHeight : options.pageWidth;
    double pageH = options.landscape ? options.pageWidth : options.pageHeight;
    double x = options.marginLeft;
    double y = pageH - options.marginTop;

    std::ostringstream textStream;
    textStream << "BT\n";
    textStream << "/F1 " << options.titleFontSize << " Tf\n";
    textStream << x << " " << y << " Td\n";
    textStream << "(" << title << ") Tj\n";

    y -= options.titleFontSize * 1.5;
    textStream << "/F1 " << options.bodyFontSize << " Tf\n";

    for (const auto& line : lines) {
        if (y < options.marginBottom) {
            break;
        }
        textStream << x << " " << y << " Td\n";
        std::string escaped;
        for (char c : line) {
            if (c == '(' || c == ')' || c == '\\') escaped += '\\';
            escaped += c;
        }
        textStream << "(" << escaped << ") Tj\n";
        y -= options.bodyFontSize * 1.4;
        textStream << "0 " << -(options.bodyFontSize * 1.4) << " Td\n";
    }

    if (y > options.marginBottom + 20) {
        textStream << "/F1 8 Tf\n";
        textStream << x << " " << options.marginBottom << " Td\n";
        textStream << "(Generated by BeatMate) Tj\n";
    }

    textStream << "ET\n";
    std::string stream = textStream.str();

    std::vector<size_t> offsets;

    pdf << "%PDF-1.4\n";

    offsets.push_back(pdf.tellp());
    pdf << "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";

    offsets.push_back(pdf.tellp());
    pdf << "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n";

    offsets.push_back(pdf.tellp());
    pdf << "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "
        << pageW << " " << pageH << "] "
        << "/Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>\nendobj\n";

    offsets.push_back(pdf.tellp());
    pdf << "4 0 obj\n<< /Length " << stream.size() << " >>\nstream\n"
        << stream << "endstream\nendobj\n";

    offsets.push_back(pdf.tellp());
    pdf << "5 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /"
        << options.fontName << " >>\nendobj\n";

    size_t xrefPos = pdf.tellp();
    pdf << "xref\n0 " << (offsets.size() + 1) << "\n";
    pdf << "0000000000 65535 f \n";
    for (auto off : offsets) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%010zu 00000 n \n", off);
        pdf << buf;
    }

    pdf << "trailer\n<< /Size " << (offsets.size() + 1) << " /Root 1 0 R >>\n";
    pdf << "startxref\n" << xrefPos << "\n%%EOF\n";

    return pdf.str();
}

std::string PdfExportService::formatTime(double seconds) const {
    int h = static_cast<int>(seconds) / 3600;
    int m = (static_cast<int>(seconds) % 3600) / 60;
    int s = static_cast<int>(seconds) % 60;
    char buf[16];
    if (h > 0) std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    else std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

}
