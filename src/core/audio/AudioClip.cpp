#include "AudioClip.h"
#include "AudioTrack.h"
#include <cmath>
#include <algorithm>

namespace BeatMate::Core {

AudioClip::AudioClip() = default;

AudioClip::AudioClip(std::shared_ptr<AudioTrack> track)
    : track_(std::move(track)) {
    if (track_) {
        endPosition_ = track_->getDuration();
    }
}

AudioClip::~AudioClip() = default;

void AudioClip::setTrack(std::shared_ptr<AudioTrack> track) {
    track_ = std::move(track);
    if (track_) {
        endPosition_ = track_->getDuration();
    }
}

float AudioClip::getLinearGain() const {
    return std::pow(10.0f, gain_.load() / 20.0f);
}

float AudioClip::getGainAtPosition(double positionInClip) const {
    if (isMuted_.load()) return 0.0f;

    float gain = getLinearGain();
    double duration = getDuration();

    if (fadeIn_ > 0.0 && positionInClip < fadeIn_) {
        gain *= static_cast<float>(positionInClip / fadeIn_);
    }

    if (fadeOut_ > 0.0 && positionInClip > (duration - fadeOut_)) {
        double fadeOutPos = duration - positionInClip;
        gain *= static_cast<float>(fadeOutPos / fadeOut_);
    }

    return std::max(0.0f, gain);
}

void AudioClip::readSamples(float* dest, size_t startFrame, size_t numFrames) const {
    if (!track_ || isMuted_.load()) {
        std::fill(dest, dest + numFrames * track_->getChannels(), 0.0f);
        return;
    }

    int sr = track_->getSampleRate();
    int channels = track_->getChannels();
    size_t clipStartFrame = static_cast<size_t>(startPosition_ * sr);
    size_t clipEndFrame = static_cast<size_t>(endPosition_ * sr);

    for (size_t i = 0; i < numFrames; ++i) {
        size_t srcFrame = clipStartFrame + startFrame + i;

        if (isLooped_.load() && clipEndFrame > clipStartFrame) {
            size_t loopLen = clipEndFrame - clipStartFrame;
            srcFrame = clipStartFrame + ((startFrame + i) % loopLen);
        }

        double posInClip = static_cast<double>(startFrame + i) / sr;
        float gain = getGainAtPosition(posInClip);

        if (srcFrame < clipEndFrame) {
            for (int ch = 0; ch < channels; ++ch) {
                dest[i * channels + ch] = track_->getSample(srcFrame, ch) * gain;
            }
        } else {
            for (int ch = 0; ch < channels; ++ch) {
                dest[i * channels + ch] = 0.0f;
            }
        }
    }
}

} // namespace BeatMate::Core
