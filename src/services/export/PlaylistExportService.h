#pragma once

#include <string>
#include <vector>
#include "../../models/Track.h"
#include "../../models/Playlist.h"

namespace BeatMate::Services::Export {

enum class PlaylistFormat { M3U, M3U8, PLS, XSPF };

class PlaylistExportService {
public:
    PlaylistExportService() = default;
    ~PlaylistExportService() = default;

    bool exportPlaylist(const Models::Playlist& playlist, const std::vector<Models::Track>& tracks,
                         PlaylistFormat format, const std::string& outputPath, bool useRelativePaths = false);
    bool exportM3U(const std::vector<Models::Track>& tracks, const std::string& outputPath, bool extended = true, bool useRelativePaths = false);
    bool exportPLS(const std::vector<Models::Track>& tracks, const std::string& outputPath);
    bool exportXSPF(const Models::Playlist& playlist, const std::vector<Models::Track>& tracks, const std::string& outputPath);

    bool importM3U(const std::string& filePath, std::vector<std::string>& outPaths);
    bool importPLS(const std::string& filePath, std::vector<std::string>& outPaths);
    bool importXSPF(const std::string& filePath, std::vector<std::string>& outPaths, std::string& outName);

    static std::string formatExtension(PlaylistFormat format);

private:
    std::string makeRelativePath(const std::string& absolutePath, const std::string& basePath) const;
};

} // namespace BeatMate::Services::Export
