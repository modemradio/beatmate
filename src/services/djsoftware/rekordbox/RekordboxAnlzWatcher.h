#pragma once

#include "../DJHistoryReader.h"

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <cstdint>

namespace BeatMate::Services::Rekordbox {

class RekordboxAnlzWatcher {
public:
    RekordboxAnlzWatcher();
    ~RekordboxAnlzWatcher();

    bool start();
    void stop();

    std::optional<BeatMate::Services::DJSoftware::PlayedTrack> readNowPlaying();

    bool isActive() const { return active_.load(); }

private:
    void threadLoop();

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> active_{false};
    mutable std::mutex mutex_;
    std::optional<BeatMate::Services::DJSoftware::PlayedTrack> pending_;
    int64_t lastSeenMtimeMs_ = 0;
    std::string lastEmittedPath_;
};

} // namespace BeatMate::Services::Rekordbox
