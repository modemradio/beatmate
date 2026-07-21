#pragma once

#include "../DJHistoryReader.h"

#include <juce_osc/juce_osc.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <cstdint>

namespace BeatMate::Services::Rekordbox {

// Integration with grufkork/rkbx_link (https://github.com/grufkork/rkbx_link, GPL-3.0).
class RkbxLinkSubprocess : private juce::OSCReceiver,
                           private juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback> {
public:
    RkbxLinkSubprocess();
    ~RkbxLinkSubprocess();

    // Locate rkbx_link.exe, write config, spawn child, start OSC listener.
    bool start(int oscPort = 9000);

    // Stop OSC listener and signal the subprocess to terminate.
    void stop();

    // True if OSC has received at least one deck status frame in the last
    bool isHealthy() const;

    // Return the current master deck's track, if any, and clear any pending
    std::optional<BeatMate::Services::DJSoftware::PlayedTrack> readNowPlaying();

private:
    // juce::OSCReceiver callback.
    void oscMessageReceived(const juce::OSCMessage& message) override;

    struct DeckState {
        std::string title;
        std::string artist;
        double   bpm      = 0.0;
        bool     master   = false;
        bool     playing  = false;
        int64_t  lastMs   = 0;
    };

    mutable std::mutex mutex_;
    DeckState decks_[5];         // index 1..4 used
    std::atomic<int64_t> lastPacketMs_{0};
    bool started_   = false;
    bool childUp_   = false;
    std::string childTag_;       // diagnostic (pid / path)
    std::string lastEmittedKey_; // "title|artist" to dedupe emissions
};

} // namespace BeatMate::Services::Rekordbox
