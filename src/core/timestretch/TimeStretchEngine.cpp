#include "TimeStretchEngine.h"
#include "SoundTouchWrapper.h"
#include "../audio/AudioTrack.h"
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

TimeStretchEngine::TimeStretchEngine() = default;
TimeStretchEngine::~TimeStretchEngine() = default;

std::shared_ptr<AudioTrack> TimeStretchEngine::stretch(const AudioTrack& track, double ratio,
                                                         StretchQuality quality) {
    spdlog::info("TimeStretchEngine: stretching by {:.2f}x (quality={})",
                 ratio, static_cast<int>(quality));

    SoundTouchWrapper st;
    st.initialize(track.getSampleRate(), track.getChannels());
    st.setTempo(ratio);

    auto result = st.process(track);
    if (result) {
        spdlog::info("TimeStretchEngine: output {:.1f}s (from {:.1f}s)",
                     result->getDuration(), track.getDuration());
    }
    return result;
}

std::shared_ptr<AudioTrack> TimeStretchEngine::pitchShift(const AudioTrack& track, double semitones) {
    spdlog::info("TimeStretchEngine: pitch shift by {:.1f} semitones", semitones);

    SoundTouchWrapper st;
    st.initialize(track.getSampleRate(), track.getChannels());
    st.setPitch(semitones);

    return st.process(track);
}

} // namespace BeatMate::Core
