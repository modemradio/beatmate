#include "SamplePad.h"
#include "../audio/AudioTrack.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace BeatMate::Core {

SamplePad::SamplePad() = default;
SamplePad::~SamplePad() = default;

void SamplePad::loadSample(std::shared_ptr<AudioTrack> sample) {
    sample_ = std::move(sample);
    position_.store(0.0);
}

void SamplePad::trigger() {
    if (!sample_) return;
    position_.store(0.0);
    playing_.store(true);
}

void SamplePad::stop() {
    playing_.store(false);
}

void SamplePad::processBlock(float* output, int numFrames, int channels) {
    if (!playing_.load() || !sample_) {
        std::memset(output, 0, numFrames * channels * sizeof(float));
        return;
    }

    float vol = volume_.load();
    double pos = position_.load();
    double speed = std::pow(2.0, pitch_.load() / 12.0);
    int srcCh = sample_->getChannels();
    size_t totalFrames = sample_->getNumFrames();

    for (int i = 0; i < numFrames; ++i) {
        size_t frame = static_cast<size_t>(pos);
        if (frame >= totalFrames) {
            if (mode_ == PadMode::Loop) {
                pos = 0.0;
                frame = 0;
            } else {
                playing_.store(false);
                std::memset(output + i * channels, 0, (numFrames - i) * channels * sizeof(float));
                break;
            }
        }

        for (int ch = 0; ch < channels; ++ch) {
            int sCh = std::min(ch, srcCh - 1);
            output[i * channels + ch] = sample_->getSample(frame, sCh) * vol;
        }
        pos += speed;
    }

    position_.store(pos);
}

} // namespace BeatMate::Core
