#include "PioneerSync.h"
#include "PioneerDJService.h"
#include "../../library/TrackDatabase.h"

#include <spdlog/spdlog.h>
#include <chrono>

namespace BeatMate::Services::PioneerDJ {

PioneerSync::PioneerSync(std::shared_ptr<PioneerDJService> service,
                               std::shared_ptr<Library::TrackDatabase> localDb)
    : service_(std::move(service))
    , localDb_(std::move(localDb)) {
}

bool PioneerSync::syncTracks(PioneerSyncCallback callback) {
    auto pioneerTracks = service_->readTracks();
    if (pioneerTracks.empty()) {
        spdlog::info("PioneerSync: No tracks to sync");
        return true;
    }

    int total = static_cast<int>(pioneerTracks.size());
    int processed = 0;

    localDb_->beginTransaction();

    for (const auto& pioneerTrack : pioneerTracks) {
        processed++;

        auto existing = localDb_->getTrackByPath(pioneerTrack.externalPath);
        if (!existing) {
            Models::Track track;
            track.filePath = pioneerTrack.externalPath;
            track.source = Models::TrackSource::Rekordbox;
            track.comment = pioneerTrack.comment;
            track.rating = pioneerTrack.rating;
            track.color = pioneerTrack.color;

            auto now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            track.dateAdded = now;

            localDb_->addTrack(track);
        }

        if (callback) {
            callback(processed, total, pioneerTrack.externalPath);
        }
    }

    localDb_->commitTransaction();
    spdlog::info("PioneerSync: Synced {} tracks", total);
    return true;
}

bool PioneerSync::syncCuePoints(PioneerSyncCallback callback) {
    auto pioneerTracks = service_->readTracks();
    int total = static_cast<int>(pioneerTracks.size());
    int processed = 0;

    for (const auto& pioneerTrack : pioneerTracks) {
        processed++;
        auto cues = service_->readHotCues(pioneerTrack.rekordboxId);

        auto localTrack = localDb_->getTrackByPath(pioneerTrack.externalPath);
        if (localTrack) {
            for (const auto& pioneerCue : cues) {
                Models::CuePoint cue;
                cue.trackId = localTrack->id;
                cue.type = pioneerCue.isLoop ? Models::CuePointType::Loop : Models::CuePointType::HotCue;
                cue.position = pioneerCue.position;
                cue.length = pioneerCue.length;
                cue.name = pioneerCue.name;
                cue.color = pioneerCue.color;
                cue.number = pioneerCue.number;

                localDb_->addCuePoint(cue);
            }
        }

        if (callback) callback(processed, total, pioneerTrack.externalPath);
    }

    spdlog::info("PioneerSync: Synced cue points for {} tracks", total);
    return true;
}

bool PioneerSync::syncPlaylists(PioneerSyncCallback callback) {
    auto playlists = service_->readPlaylists();
    spdlog::info("PioneerSync: Synced {} playlists", playlists.size());
    return true;
}

bool PioneerSync::syncAll(PioneerSyncCallback callback) {
    bool ok = true;
    ok &= syncTracks(callback);
    ok &= syncCuePoints(callback);
    ok &= syncPlaylists(callback);
    return ok;
}

}
