#pragma once

#include <string>
#include <vector>
#include <memory>

#include "../../../models/PioneerTrack.h"
#include "../../../models/Playlist.h"

namespace BeatMate::Services::PioneerDJ {

class PioneerDatabase;
class PioneerEnvironment;

class PioneerDJService {
public:
    PioneerDJService();
    ~PioneerDJService();

    bool initialize();
    bool isAvailable() const;

    std::vector<Models::PioneerTrack> readTracks();
    std::vector<Models::Playlist> readPlaylists();
    std::vector<Models::PioneerDJCue> readHotCues(const std::string& pioneerTrackId);

    bool syncWithLocalDatabase();

private:
    std::unique_ptr<PioneerDatabase> database_;
    std::unique_ptr<PioneerEnvironment> environment_;
    bool initialized_ = false;
};

} // namespace BeatMate::Services::PioneerDJ
