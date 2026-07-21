#pragma once
#include <juce_events/juce_events.h>
#include <functional>
#include <string>
#include <juce_core/juce_core.h>
#include "../../models/Track.h"

namespace BeatMate::Services::Realtime {

using TrackChangedCallback = std::function<void(const std::string& title, const std::string& artist, double bpm)>;

class RekordboxMonitor : private juce::Timer {
public:
    RekordboxMonitor();
    ~RekordboxMonitor() override;
    void start(int intervalMs = 2000);
    void stop();
    void setDatabasePath(const std::string& path) { dbPath_ = path; }
    void setTrackChangedCallback(TrackChangedCallback cb) { trackChangedCallback_ = std::move(cb); }

private:
    void timerCallback() override;

    std::string dbPath_;
    std::string lastTrackHash_;
    TrackChangedCallback trackChangedCallback_;
};
} // namespace BeatMate::Services::Realtime
