#pragma once

#include "../DJHistoryReader.h"

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <cstdint>

namespace BeatMate::Services::Rekordbox {

// RekordboxWalWatcher — tails master.db-wal and emits a PlayedTrack each time
class RekordboxWalWatcher {
public:
    RekordboxWalWatcher();
    ~RekordboxWalWatcher();

    bool start();                      // idempotent; spawns worker thread.
    void stop();                       // idempotent; joins worker.

    // Returns the most recent unread new-history hit, or nullopt. Clears
    std::optional<BeatMate::Services::DJSoftware::PlayedTrack> readNowPlaying();

    // True once the watcher has successfully decrypted master.db at least
    bool isActive() const { return active_.load(); }

private:
    void threadLoop();

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> active_{false};

    mutable std::mutex mutex_;
    std::optional<BeatMate::Services::DJSoftware::PlayedTrack> pending_;
    // djmdSongHistory.ID is a VARCHAR UUID in Rekordbox 6/7, not an integer.
    std::string lastSeenCreatedAt_;
    // Watermark for the "WAL touched but no new row" fallback emission.
    std::string lastEmittedFallback_;
    int64_t lastWalSize_       = -1;
    int64_t lastWalMtimeMs_    = 0;
};

} // namespace BeatMate::Services::Rekordbox
