#include "TrackChangeDetector.h"
#include <spdlog/spdlog.h>
namespace BeatMate::Services::Realtime {
void TrackChangeDetector::notifyTrackChange(const Models::Track& track) {
    if (track.id != currentTrack_.id) {
        currentTrack_ = track;
        spdlog::info("TrackChangeDetector: Track changed to '{}' by '{}'", track.title, track.artist);
        if (callback_) callback_(track);
    }
}
} // namespace BeatMate::Services::Realtime
