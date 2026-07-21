#pragma once

#include "../DJHistoryReader.h"

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <cstdint>

namespace BeatMate::Services::Rekordbox {

// RekordboxProLink - passive Pioneer PRO DJ LINK listener.
//   https://djl-analysis.deepsymmetry.org/djl-analysis/vcdj.html
// Magic header of every DJ-Link packet: 51 73 70 74 31 57 6d 4a 4f 4c ("Qspt1WmJOL")
class RekordboxProLink {
public:
    RekordboxProLink();
    ~RekordboxProLink();

    // Start the background listener thread. Idempotent. Logs via spdlog.
    bool start();

    // Stop and join the listener thread. Idempotent.
    void stop();

    // Return the latest packet-derived "master deck" track, if any.
    std::optional<BeatMate::Services::DJSoftware::PlayedTrack> readNowPlaying();

    // True if a status packet has been received within the last ~5 s.
    bool isReceiving() const;

    // Rekordbox DB ID of the currently-master (or fallback: playing) deck, 0 if none.
    uint32_t currentMasterTrackId() const;

private:
    struct DeckState {
        uint32_t rekordboxId = 0;
        double   bpm         = 0.0;
        bool     playing     = false;
        bool     master      = false;
        int64_t  lastSeenMs  = 0;
    };

    void threadLoop();
    void handlePacket(const uint8_t* data, size_t len);

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<int64_t> lastPacketMs_{0};

    mutable std::mutex mutex_;
    DeckState decks_[6];     // players 1..6 indexed (index 0 unused)
    int masterPlayer_ = 0;   // 0 = none
};

} // namespace BeatMate::Services::Rekordbox
