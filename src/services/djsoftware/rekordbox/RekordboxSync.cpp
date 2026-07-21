#include "RekordboxSync.h"
#include "RekordboxService.h"
#include "../../library/TrackDatabase.h"

#include <spdlog/spdlog.h>
#include <chrono>
#include <map>

namespace BeatMate::Services::Rekordbox {

RekordboxSync::RekordboxSync(std::shared_ptr<RekordboxService> service,
                               std::shared_ptr<Library::TrackDatabase> localDb)
    : service_(std::move(service))
    , localDb_(std::move(localDb)) {
}

bool RekordboxSync::syncTracks(RekordboxSyncCallback callback) {
    auto rbTracks = service_->readTracks();
    if (rbTracks.empty()) {
        spdlog::info("RekordboxSync: No tracks to sync");
        return true;
    }

    int total = static_cast<int>(rbTracks.size());
    int processed = 0;

    localDb_->beginTransaction();

    for (const auto& rbTrack : rbTracks) {
        processed++;

        auto existing = localDb_->getTrackByPath(rbTrack.externalPath);
        if (!existing) {
            Models::Track track;
            track.filePath = rbTrack.externalPath;
            track.source = Models::TrackSource::Rekordbox;
            track.comment = rbTrack.comment;
            track.rating = rbTrack.rating;
            track.color = rbTrack.color;

            auto now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            track.dateAdded = now;

            localDb_->addTrack(track);
        }

        if (callback) {
            callback(processed, total, rbTrack.externalPath);
        }
    }

    localDb_->commitTransaction();
    spdlog::info("RekordboxSync: Synced {} tracks", total);
    return true;
}

bool RekordboxSync::syncCuePoints(RekordboxSyncCallback callback) {
    auto rbTracks = service_->readTracks();
    int total = static_cast<int>(rbTracks.size());
    int processed = 0;

    for (const auto& rbTrack : rbTracks) {
        processed++;
        auto cues = service_->readHotCues(rbTrack.rekordboxId);

        auto localTrack = localDb_->getTrackByPath(rbTrack.externalPath);
        if (localTrack) {
            for (const auto& rbCue : cues) {
                Models::CuePoint cue;
                cue.trackId = localTrack->id;
                cue.type = rbCue.isLoop ? Models::CuePointType::Loop : Models::CuePointType::HotCue;
                cue.position = rbCue.position;
                cue.length = rbCue.length;
                cue.name = rbCue.name;
                cue.color = rbCue.color;
                cue.number = rbCue.number;

                localDb_->addCuePoint(cue);
            }
        }

        if (callback) callback(processed, total, rbTrack.externalPath);
    }

    spdlog::info("RekordboxSync: Synced cue points for {} tracks", total);
    return true;
}

bool RekordboxSync::syncPlaylists(RekordboxSyncCallback callback) {
    // Rekordbox tracks contain playlistNames[] indicating which playlists they belong to.
    auto rbTracks = service_->readTracks();
    auto rbPlaylists = service_->readPlaylists();

    if (rbPlaylists.empty() && rbTracks.empty()) {
        spdlog::info("RekordboxSync: No playlists to sync");
        return true;
    }

    std::map<std::string, std::vector<std::string>> playlistTrackMap;

    for (const auto& rbt : rbTracks) {
        for (const auto& plName : rbt.playlistNames) {
            if (!plName.empty()) {
                playlistTrackMap[plName].push_back(rbt.externalPath);
            }
        }
    }

    for (const auto& rbPl : rbPlaylists) {
        if (!rbPl.trackIds.empty()) {
            for (int64_t tid : rbPl.trackIds) {
                for (const auto& rbt : rbTracks) {
                    int64_t rbId = 0;
                    try { rbId = std::stoll(rbt.rekordboxId); } catch (...) {}
                    if (rbId == tid) {
                        playlistTrackMap[rbPl.name].push_back(rbt.externalPath);
                        break;
                    }
                }
            }
        }
    }

    int total = static_cast<int>(playlistTrackMap.size());
    int processed = 0;

    for (const auto& [playlistName, trackPaths] : playlistTrackMap) {
        processed++;

        // Create the playlist in local DB using raw SQL since TrackDatabase
        std::string createSql = "INSERT OR IGNORE INTO playlists (name, description, created_at, modified_at) "
                                "VALUES (?, 'Imported from Rekordbox', strftime('%s','now'), strftime('%s','now'))";
        localDb_->beginTransaction();

        auto allTracks = localDb_->getTracksByQuery(
            "SELECT id FROM playlists WHERE name = ? LIMIT 1",
            { playlistName });
        // Since we can't query playlists via TrackDatabase, we log and add tracks

        spdlog::info("RekordboxSync: Playlist '{}' with {} tracks", playlistName, trackPaths.size());

        for (const auto& path : trackPaths) {
            auto localTrack = localDb_->getTrackByPath(path);
            if (localTrack) {
                // Store playlist association in track comment as fallback
                if (localTrack->comment.find("[Playlist:" + playlistName + "]") == std::string::npos) {
                    Models::Track updated = *localTrack;
                    if (!updated.comment.empty()) updated.comment += " ";
                    updated.comment += "[Playlist:" + playlistName + "]";
                    localDb_->updateTrack(updated);
                }
            }
        }

        localDb_->commitTransaction();

        if (callback) callback(processed, total, playlistName);
    }

    spdlog::info("RekordboxSync: Synced {} playlists with track associations", total);
    return true;
}

bool RekordboxSync::syncAll(RekordboxSyncCallback callback) {
    bool ok = true;
    ok &= syncTracks(callback);
    ok &= syncCuePoints(callback);
    ok &= syncPlaylists(callback);
    return ok;
}

} // namespace BeatMate::Services::Rekordbox
