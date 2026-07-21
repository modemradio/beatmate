#pragma once

#include <string>
#include <vector>
#include <memory>

#include <sqlite3.h>

#include "../../../models/RekordboxTrack.h"
#include "../../../models/Playlist.h"

namespace BeatMate::Services::Rekordbox {

class RekordboxDatabase {
public:
    RekordboxDatabase();
    ~RekordboxDatabase();

    bool openDatabase(const std::string& path, const std::string& key = "");
    void close();
    bool isOpen() const;

    sqlite3* rawHandle() const { return db_; }

    std::vector<Models::RekordboxTrack> readAllTracks();
    std::vector<Models::Playlist> readPlaylists();
    std::vector<Models::RekordboxCue> readCuePoints(const std::string& contentId);

    struct RekordboxPlaylistInfo {
        std::string externalId;        // djmdPlaylist.ID (VARCHAR UUID in v7+, INT in v6-)
        std::string name;
        std::string parentExternalId;  // djmdPlaylist.ParentID ("" = root)
        int attribute = 0;             // 0 = playlist, 1 = folder
        int seq = 0;
    };
    std::vector<RekordboxPlaylistInfo> readPlaylistsRich();

    std::vector<std::string> readPlaylistContentIds(const std::string& playlistExternalId);

    std::string readContentFilePath(const std::string& contentId);

private:
    Models::RekordboxTrack trackFromStatement(sqlite3_stmt* stmt, bool isV6Schema);
    sqlite3* db_ = nullptr;
    bool isOpen_ = false;
};

} // namespace BeatMate::Services::Rekordbox
