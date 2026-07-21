#pragma once

#include "DJHistoryReader.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace BeatMate::Services::DJSoftware {

// fusionne l'historique de lecture récent de tous les logiciels DJ détectés
class UnifiedDJHistory {
public:
    UnifiedDJHistory();
    ~UnifiedDJHistory();

    std::vector<PlayedTrack> getRecent(int maxTracks = 500);

    void startPolling(int intervalSec = 10);
    void stopPolling();

    // appelé sur le thread de polling
    std::function<void(const PlayedTrack&)> onNewPlay;

    // pilote l'UI DJConnectionPanel
    enum class Status {
        Disconnected,  // not enabled
        Installed,     // enabled but nothing observed / app not running
        Connected,     // actively polling and got data
        Error          // last attempt failed (e.g. Rekordbox SQLCipher)
    };

    bool start(const std::string& sourceName);
    void stop (const std::string& sourceName);
    Status status(const std::string& sourceName) const;
    std::optional<PlayedTrack> nowPlaying(const std::string& sourceName) const;

    // appelé sur le thread de polling
    std::function<void(const std::string& source, Status s)> onStatusChanged;

private:
    void pollOnce();
    void setStatus(const std::string& src, Status s);
    static std::string trackKey(const PlayedTrack& t);

    std::vector<std::unique_ptr<DJHistoryReader>> readers_;

    std::atomic<bool> polling_{false};
    std::thread pollThread_;
    int intervalSec_ = 10;

    std::mutex seenMutex_;
    std::unordered_set<std::string> seenKeys_;

    mutable std::mutex stateMutex_;
    std::unordered_map<std::string, bool>   enabled_;
    std::unordered_map<std::string, Status> statuses_;
    std::unordered_map<std::string, PlayedTrack> nowPlaying_;
};

} // namespace BeatMate::Services::DJSoftware
