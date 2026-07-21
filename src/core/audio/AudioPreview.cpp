#include "AudioPreview.h"
#include "AudioFileReader.h"
#include "AudioPlayer.h"
#include "AudioTrack.h"
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

AudioPreview::AudioPreview()
    : reader_(std::make_unique<AudioFileReader>()),
      player_(std::make_unique<AudioPlayer>()) {
}

AudioPreview::~AudioPreview() {
    stop();
}

bool AudioPreview::previewTrack(const std::string& path, double startSec, double durationSec) {
    stop();

    previewTrack_ = reader_->readRange(path, startSec, durationSec);
    if (!previewTrack_) {
        spdlog::error("AudioPreview: failed to load {}", path);
        return false;
    }

    player_->loadTrack(previewTrack_);
    player_->play();
    playing_.store(true);

    spdlog::info("AudioPreview: playing {} from {:.1f}s ({:.1f}s)", path, startSec, durationSec);
    return true;
}

void AudioPreview::stop() {
    if (player_) player_->stop();
    playing_.store(false);
}

void AudioPreview::processBlock(float* output, int numFrames, int channels) {
    if (!playing_.load() || !player_) {
        std::memset(output, 0, numFrames * channels * sizeof(float));
        return;
    }

    player_->processBlock(output, numFrames, channels);

    if (player_->isStopped()) {
        playing_.store(false);
    }
}

double AudioPreview::getPosition() const {
    if (!player_) return 0.0;
    return player_->getPosition();
}

void AudioPreview::setVolume(float vol) {
    if (player_) player_->setVolume(vol);
}

} // namespace BeatMate::Core
