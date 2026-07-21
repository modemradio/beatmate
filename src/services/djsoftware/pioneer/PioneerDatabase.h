#pragma once

#include <string>
#include <vector>
#include <memory>

#include <sqlite3.h>

#include "../../../models/PioneerTrack.h"
#include "../../../models/Playlist.h"

namespace BeatMate::Services::PioneerDJ {

class PioneerDatabase {
public:
    PioneerDatabase();
    ~PioneerDatabase();

    bool openDatabase(const std::string& path, const std::string& key = "");
    void close();
    bool isOpen() const;

    std::vector<Models::PioneerTrack> readAllTracks();
    std::vector<Models::Playlist> readPlaylists();
    std::vector<Models::PioneerDJCue> readCuePoints(const std::string& contentId);

private:
    Models::PioneerTrack trackFromStatement(sqlite3_stmt* stmt, bool isV6Schema);
    sqlite3* db_ = nullptr;
    bool isOpen_ = false;
};

} // namespace BeatMate::Services::PioneerDJ
