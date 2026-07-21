#pragma once
#include <juce_events/juce_events.h>

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <atomic>
#include <set>
#include <unordered_map>

#include <juce_core/juce_core.h>

#include "DJSoftwareManager.h"
#include "../library/TrackDatabase.h"
#include "../../models/CuePoint.h"

namespace BeatMate::Services::Library { class PlaylistManager; }
namespace BeatMate::Services::DJSoftware {

struct SyncProgress {
    DJSoftwareType software;
    int total = 0;
    int processed = 0;
    std::string currentItem;
    float percentage() const { return total > 0 ? (static_cast<float>(processed) / total) * 100.0f : 0.0f; }
};

struct ExternalPlaylistDescriptor {
    std::string externalId;
    std::string name;
    std::string parentPath;
    int trackCount = 0;
    bool isFolder = false;
};

using SyncProgressCallback = std::function<void(const SyncProgress&)>;
using SyncCompleteCallback = std::function<void(DJSoftwareType, bool success, const std::string& message)>;
using SyncStartedCallback = std::function<void(int softwareType)>;
using SyncProgressNotifyCallback = std::function<void(int softwareType, int processed, int total)>;
using SyncCompletedCallback = std::function<void(int softwareType, bool success)>;

class CollectionSyncService : private juce::Timer {

public:
    explicit CollectionSyncService(std::shared_ptr<DJSoftwareManager> manager,
                                    std::shared_ptr<BeatMate::Services::Library::TrackDatabase> trackDb = nullptr);
    ~CollectionSyncService() override;

    void start(int intervalSeconds = 60);
    void stop();
    bool isRunning() const;

    void syncAll();
    void syncSoftware(DJSoftwareType type);

    // Filtered import (Import hub): only the given track paths are synced,
    bool syncSoftwareFiltered(DJSoftwareType type, const std::vector<std::string>& onlyTrackPaths);
    std::vector<ExternalPlaylistDescriptor> listPlaylists(DJSoftwareType type);
    std::vector<std::string> playlistTrackPaths(DJSoftwareType type,
                                                const std::set<std::string>& externalIds);
    void requestCancel();
    bool wasCancelled() const { return cancelRequested_.load(); }

    void setProgressCallback(SyncProgressCallback callback) { progressCallback_ = std::move(callback); }
    void setCompleteCallback(SyncCompleteCallback callback) { completeCallback_ = std::move(callback); }
    void setSyncStartedCallback(SyncStartedCallback callback) { syncStartedCallback_ = std::move(callback); }
    void setSyncProgressNotifyCallback(SyncProgressNotifyCallback callback) { syncProgressNotifyCallback_ = std::move(callback); }
    void setSyncCompletedCallback(SyncCompletedCallback callback) { syncCompletedCallback_ = std::move(callback); }

    bool exportToSoftware(DJSoftwareType type, const std::vector<Models::Track>& tracks);
    bool exportPlaylistToSoftware(DJSoftwareType type, const std::string& playlistName, const std::vector<Models::Track>& tracks);

    // Synchro playlists (appli DJ -> PlaylistManager BeatMate)
    void setPlaylistManager(std::shared_ptr<BeatMate::Services::Library::PlaylistManager> pm) {
        playlistManager_ = std::move(pm);
    }
    int syncPlaylistsFor(DJSoftwareType type);     // returns number of playlists upserted
    int syncPlaylistsFor(DJSoftwareType type, const std::set<std::string>& onlyExternalIds);
    int syncAllPlaylists();                        // calls syncPlaylistsFor on all installed

    std::map<DJSoftwareType, SyncStatus> getStatus() const;

private:
    void timerCallback() override;
    void performSync(DJSoftwareType type);
    int syncPlaylistsImpl(DJSoftwareType type, const std::set<std::string>& onlyExternalIds);
    bool passesPathFilter(const std::string& path) const;

    std::set<std::string> pathFilter_;
    std::atomic<bool> cancelRequested_{false};

    std::shared_ptr<DJSoftwareManager> manager_;
    std::shared_ptr<BeatMate::Services::Library::TrackDatabase> trackDb_;
    std::shared_ptr<BeatMate::Services::Library::PlaylistManager> playlistManager_;
    int insertTrackIfNew(const Models::Track& track,
                         const std::vector<Models::CuePoint>& cuePoints = {});
    void rebuildTrackIndex();
    std::vector<Models::Track> resolveExistingTracks(const Models::Track& incoming);
    std::unordered_map<std::string, int64_t> pathIndex_;
    std::unordered_multimap<std::string, int64_t> nameIndex_;
    bool trackIndexBuilt_ = false;
    std::atomic<bool> running_{false};
    std::atomic<bool> syncing_{false};
    std::map<DJSoftwareType, SyncStatus> status_;
    SyncProgressCallback progressCallback_;
    SyncCompleteCallback completeCallback_;
    SyncStartedCallback syncStartedCallback_;
    SyncProgressNotifyCallback syncProgressNotifyCallback_;
    SyncCompletedCallback syncCompletedCallback_;
};

} // namespace BeatMate::Services::DJSoftware
