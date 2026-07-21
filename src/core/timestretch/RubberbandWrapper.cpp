#include "RubberbandWrapper.h"
#include "../audio/AudioTrack.h"
#include "SoundTouchWrapper.h"
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

struct RubberbandWrapper::Impl {
    SoundTouchWrapper soundTouch;
};

RubberbandWrapper::RubberbandWrapper() : impl_(std::make_unique<Impl>()) {}
RubberbandWrapper::~RubberbandWrapper() = default;

bool RubberbandWrapper::isAvailable() {
    // Rubberband non embarqué — fallback SoundTouch
    return true;
}

void RubberbandWrapper::initialize(int sampleRate, int channels) {
    sampleRate_ = sampleRate;
    channels_ = channels;
    impl_->soundTouch.initialize(sampleRate, channels);
}

void RubberbandWrapper::setTimeRatio(double ratio) {
    timeRatio_ = ratio;
    impl_->soundTouch.setTempo(ratio);
}

void RubberbandWrapper::setPitchScale(double scale) {
    pitchScale_ = scale;
    double semitones = 12.0 * std::log2(scale);
    impl_->soundTouch.setPitch(semitones);
}

std::shared_ptr<AudioTrack> RubberbandWrapper::process(const AudioTrack& input) {
    impl_->soundTouch.initialize(input.getSampleRate(), input.getChannels());
    impl_->soundTouch.setTempo(timeRatio_);
    if (pitchScale_ != 1.0) {
        double semitones = 12.0 * std::log2(pitchScale_);
        impl_->soundTouch.setPitch(semitones);
    }

    auto result = impl_->soundTouch.process(input);
    if (result) {
        spdlog::debug("RubberbandWrapper: Processed via SoundTouch (ratio={:.2f}, pitch={:.2f})",
            timeRatio_, pitchScale_);
    }
    return result;
}

} // namespace BeatMate::Core
