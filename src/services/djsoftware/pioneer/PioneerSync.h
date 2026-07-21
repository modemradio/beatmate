#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

#include "../../../models/PioneerTrack.h"

namespace BeatMate::Services::Library { class TrackDatabase; }

namespace BeatMate::Services::PioneerDJ {

class PioneerDJService;

using PioneerSyncCallback = std::function<void(int processed, int total, const std::string& current)>;

class PioneerSync {
public:
    PioneerSync(std::shared_ptr<PioneerDJService> service, std::shared_ptr<Library::TrackDatabase> localDb);
    ~PioneerSync() = default;

    bool syncTracks(PioneerSyncCallback callback = nullptr);
    bool syncCuePoints(PioneerSyncCallback callback = nullptr);
    bool syncPlaylists(PioneerSyncCallback callback = nullptr);
    bool syncAll(PioneerSyncCallback callback = nullptr);

private:
    std::shared_ptr<PioneerDJService> service_;
    std::shared_ptr<Library::TrackDatabase> localDb_;
};

} // namespace BeatMate::Services::PioneerDJ
