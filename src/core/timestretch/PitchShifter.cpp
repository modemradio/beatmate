#include "PitchShifter.h"
#include "../audio/AudioTrack.h"
#include "SoundTouchWrapper.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

PitchShifter::PitchShifter() = default;
PitchShifter::~PitchShifter() = default;

std::shared_ptr<AudioTrack> PitchShifter::shift(const AudioTrack& input, double semitones) {
    semitones = std::clamp(semitones, -12.0, 12.0);
    spdlog::info("PitchShifter: shifting by {:.1f} semitones", semitones);

    SoundTouchWrapper st;
    st.initialize(input.getSampleRate(), input.getChannels());
    st.setPitch(semitones);

    return st.process(input);
}

} // namespace BeatMate::Core
