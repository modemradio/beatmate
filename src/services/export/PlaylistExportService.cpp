#include "PlaylistExportService.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <regex>
#include <algorithm>

namespace fs = std::filesystem;

namespace BeatMate::Services::Export {

bool PlaylistExportService::exportPlaylist(const Models::Playlist& playlist,
                                            const std::vector<Models::Track>& tracks,
                                            PlaylistFormat format, const std::string& outputPath,
                                            bool useRelativePaths) {
    switch (format) {
        case PlaylistFormat::M3U:
        case PlaylistFormat::M3U8:
            return exportM3U(tracks, outputPath, true, useRelativePaths);
        case PlaylistFormat::PLS:
            return exportPLS(tracks, outputPath);
        case PlaylistFormat::XSPF:
            return exportXSPF(playlist, tracks, outputPath);
    }
    return false;
}

bool PlaylistExportService::exportM3U(const std::vector<Models::Track>& tracks,
                                        const std::string& outputPath, bool extended,
                                        bool useRelativePaths) {
    std::ofstream ofs(outputPath);
    if (!ofs.is_open()) return false;

    std::string basePath = fs::path(outputPath).parent_path().string();

    if (extended) ofs << "#EXTM3U\n";

    for (const auto& track : tracks) {
        if (extended) {
            ofs << "#EXTINF:" << static_cast<int>(track.duration) << ","
                << track.artist << " - " << track.title << "\n";
        }
        if (useRelativePaths) {
            ofs << makeRelativePath(track.filePath, basePath) << "\n";
        } else {
            ofs << track.filePath << "\n";
        }
    }

    spdlog::info("PlaylistExportService: Exported M3U with {} tracks to {}", tracks.size(), outputPath);
    return true;
}

bool PlaylistExportService::exportPLS(const std::vector<Models::Track>& tracks,
                                        const std::string& outputPath) {
    std::ofstream ofs(outputPath);
    if (!ofs.is_open()) return false;

    ofs << "[playlist]\n";
    ofs << "NumberOfEntries=" << tracks.size() << "\n\n";

    for (size_t i = 0; i < tracks.size(); ++i) {
        int n = static_cast<int>(i + 1);
        ofs << "File" << n << "=" << tracks[i].filePath << "\n";
        ofs << "Title" << n << "=" << tracks[i].artist << " - " << tracks[i].title << "\n";
        ofs << "Length" << n << "=" << static_cast<int>(tracks[i].duration) << "\n\n";
    }

    ofs << "Version=2\n";
    spdlog::info("PlaylistExportService: Exported PLS with {} tracks to {}", tracks.size(), outputPath);
    return true;
}

bool PlaylistExportService::exportXSPF(const Models::Playlist& playlist,
                                         const std::vector<Models::Track>& tracks,
                                         const std::string& outputPath) {
    std::ofstream ofs(outputPath);
    if (!ofs.is_open()) return false;

    ofs << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ofs << "<playlist version=\"1\" xmlns=\"http://xspf.org/ns/0/\">\n";
    ofs << "  <title>" << playlist.name << "</title>\n";
    if (!playlist.description.empty()) {
        ofs << "  <annotation>" << playlist.description << "</annotation>\n";
    }
    ofs << "  <creator>BeatMate</creator>\n";
    ofs << "  <trackList>\n";

    for (const auto& track : tracks) {
        ofs << "    <track>\n";
        ofs << "      <location>file:///" << track.filePath << "</location>\n";
        ofs << "      <title>" << track.title << "</title>\n";
        ofs << "      <creator>" << track.artist << "</creator>\n";
        if (!track.album.empty()) ofs << "      <album>" << track.album << "</album>\n";
        ofs << "      <duration>" << static_cast<int>(track.duration * 1000) << "</duration>\n";
        ofs << "    </track>\n";
    }

    ofs << "  </trackList>\n";
    ofs << "</playlist>\n";

    spdlog::info("PlaylistExportService: Exported XSPF with {} tracks to {}", tracks.size(), outputPath);
    return true;
}

bool PlaylistExportService::importM3U(const std::string& filePath, std::vector<std::string>& outPaths) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) return false;

    std::string basePath = fs::path(filePath).parent_path().string();
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (!fs::path(line).is_absolute()) {
            line = basePath + "/" + line;
        }
        outPaths.push_back(line);
    }
    return !outPaths.empty();
}

bool PlaylistExportService::importPLS(const std::string& filePath, std::vector<std::string>& outPaths) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) return false;

    std::string line;
    std::regex fileRegex(R"(File\d+=(.+))");
    while (std::getline(ifs, line)) {
        std::smatch match;
        if (std::regex_match(line, match, fileRegex)) {
            outPaths.push_back(match[1].str());
        }
    }
    return !outPaths.empty();
}

bool PlaylistExportService::importXSPF(const std::string& filePath,
                                         std::vector<std::string>& outPaths,
                                         std::string& outName) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());

    std::regex locRegex(R"(<location>file:///(.+?)</location>)");
    auto begin = std::sregex_iterator(content.begin(), content.end(), locRegex);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        outPaths.push_back((*it)[1].str());
    }

    std::regex titleRegex(R"(<title>(.+?)</title>)");
    std::smatch titleMatch;
    if (std::regex_search(content, titleMatch, titleRegex)) {
        outName = titleMatch[1].str();
    }

    return !outPaths.empty();
}

std::string PlaylistExportService::formatExtension(PlaylistFormat format) {
    switch (format) {
        case PlaylistFormat::M3U:  return "m3u";
        case PlaylistFormat::M3U8: return "m3u8";
        case PlaylistFormat::PLS:  return "pls";
        case PlaylistFormat::XSPF: return "xspf";
    }
    return "m3u";
}

std::string PlaylistExportService::makeRelativePath(const std::string& absolutePath,
                                                      const std::string& basePath) const {
    try {
        return fs::relative(absolutePath, basePath).string();
    } catch (...) {
        return absolutePath;
    }
}

}
