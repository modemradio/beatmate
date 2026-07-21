#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

#include "../../../models/RekordboxTrack.h"

namespace BeatMate::Services::Library { class TrackDatabase; }

namespace BeatMate::Services::Rekordbox {

class RekordboxService;

using RekordboxSyncCallback = std::function<void(int processed, int total, const std::string& current)>;

class RekordboxSync {
public:
    RekordboxSync(std::shared_ptr<RekordboxService> service, std::shared_ptr<Library::TrackDatabase> localDb);
    ~RekordboxSync() = default;

    bool syncTracks(RekordboxSyncCallback callback = nullptr);
    bool syncCuePoints(RekordboxSyncCallback callback = nullptr);
    bool syncPlaylists(RekordboxSyncCallback callback = nullptr);
    bool syncAll(RekordboxSyncCallback callback = nullptr);

private:
    std::shared_ptr<RekordboxService> service_;
    std::shared_ptr<Library::TrackDatabase> localDb_;
};

}
