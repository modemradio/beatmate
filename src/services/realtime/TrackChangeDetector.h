#pragma once
#include <functional>
#include <string>
#include "../../models/Track.h"
namespace BeatMate::Services::Realtime {
using TrackChangedCallback = std::function<void(const Models::Track& newTrack)>;
class TrackChangeDetector {
public:
    TrackChangeDetector() = default;
    void onTrackChanged(TrackChangedCallback callback) { callback_ = std::move(callback); }
    void notifyTrackChange(const Models::Track& track);
    Models::Track getCurrentTrack() const { return currentTrack_; }
private:
    TrackChangedCallback callback_;
    Models::Track currentTrack_;
};
} // namespace BeatMate::Services::Realtime
