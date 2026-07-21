#pragma once

#include <string>
#include <vector>

namespace BeatMate::Services::VirtualDJ {

struct VDJPlaylistEntry {
    std::string filePath;    // absolute or relative track path
    int position = 0;
};

struct VDJPlaylistInfo {
    std::string fullPath;    // logical path inside the VDJ Playlists tree, e.g. "/Closing Set"
    std::string name;        // leaf name
    std::string parentPath;  // logical parent path, "" for root-level entries
    std::string filePath;    // source .vdjfolder / .m3u / .m3u8 file
    bool isFolder = false;
    std::vector<VDJPlaylistEntry> entries;
};

class VirtualDJPlaylistReader {
public:
    VirtualDJPlaylistReader() = default;
    ~VirtualDJPlaylistReader() = default;

    std::vector<VDJPlaylistInfo> readPlaylists(const std::string& vdjRoot = "");

    static std::string findDefaultVdjRoot();
};

} // namespace BeatMate::Services::VirtualDJ
