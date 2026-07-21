#include "AudioPlayer.h"
#include "AudioTrack.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

AudioPlayer::AudioPlayer() {
    spdlog::debug("AudioPlayer created");
}

AudioPlayer::~AudioPlayer() {
    stop();
}

void AudioPlayer::loadTrack(std::shared_ptr<AudioTrack> track) {
    stop();
    stoppedByLimit_.store(false);
    std::atomic_store(&track_, std::move(track));
    positionFrames_.store(0.0);
    double maxSec = maxPlaybackSeconds_.load();
    double totalPlayed = totalPlayedSeconds_.load();
    if (maxSec > 0.0 && totalPlayed >= maxSec) {
        trialExpired_.store(true);
        stoppedByLimit_.store(true);
        spdlog::warn("AudioPlayer: trial already expired ({:.0f}s played), cannot load new track for playback", totalPlayed);
    }
    spdlog::info("AudioPlayer: track loaded, duration={:.2f}s, trialLimit={:.0f}s, totalPlayed={:.0f}s",
                 getDuration(), maxSec, totalPlayed);
}

void AudioPlayer::play() {
    if (!std::atomic_load(&track_)) return;
    if (trialExpired_.load()) {
        spdlog::warn("AudioPlayer: play blocked - trial expired");
        stoppedByLimit_.store(true);
        return;
    }
    state_.store(PlayerState::Playing);
    spdlog::debug("AudioPlayer: play");
}

void AudioPlayer::pause() {
    if (state_.load() == PlayerState::Playing) {
        state_.store(PlayerState::Paused);
        spdlog::debug("AudioPlayer: paused at {:.2f}s", getPosition());
    }
}

void AudioPlayer::stop() {
    state_.store(PlayerState::Stopped);
    positionFrames_.store(0.0);
    spdlog::debug("AudioPlayer: stopped");
}

void AudioPlayer::seek(double seconds) {
    setPosition(seconds);
}

void AudioPlayer::setPosition(double seconds) {
    auto track = std::atomic_load(&track_);
    if (!track) return;
    double frame = seconds * track->getSampleRate();
    frame = std::clamp(frame, 0.0, static_cast<double>(track->getNumFrames()));
    positionFrames_.store(frame);
}

double AudioPlayer::getPosition() const {
    auto track = std::atomic_load(&track_);
    if (!track) return 0.0;
    return positionFrames_.load() / track->getSampleRate();
}

double AudioPlayer::getDuration() const {
    auto track = std::atomic_load(&track_);
    if (!track) return 0.0;
    return track->getDuration();
}

void AudioPlayer::setVolume(float linearVolume) {
    volume_.store(std::clamp(linearVolume, 0.0f, 2.0f));
}

void AudioPlayer::setSpeed(double speed) {
    speed_.store(std::clamp(speed, 0.1, 4.0));
}

void AudioPlayer::setPitch(double semitones) {
    pitch_.store(std::clamp(semitones, -12.0, 12.0));
}

void AudioPlayer::setPositionCallback(PositionCallback cb) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    positionCallback_ = std::move(cb);
}

void AudioPlayer::processBlock(float* output, int numFrames, int channels) {
    // Copie locale atomique du shared_ptr : sinon data race avec loadTrack().
    auto track = std::atomic_load(&track_);

    if (!track || state_.load() != PlayerState::Playing) {
        std::memset(output, 0, numFrames * channels * sizeof(float));
        return;
    }

    // Thread audio : acces PCM lock-free, aucun mutex ici.
    const float* rawData = track->getRawData();
    int trackChannels = track->getChannels();
    size_t totalFrames = track->getNumFrames();
    size_t totalSamples = track->getTotalSamples();

    if (!rawData || totalFrames == 0) {
        std::memset(output, 0, numFrames * channels * sizeof(float));
        return;
    }

    double maxSec = maxPlaybackSeconds_.load(std::memory_order_relaxed);
    double sampleRate = track->getSampleRate();
    double currentSec = positionFrames_.load(std::memory_order_relaxed) / sampleRate;
    double blockDurationSec = static_cast<double>(numFrames) / sampleRate;

    if (maxSec > 0.0) {
        double prevTotal = totalPlayedSeconds_.load(std::memory_order_relaxed);
        totalPlayedSeconds_.store(prevTotal + blockDurationSec, std::memory_order_relaxed);
    }

    {
        static int debugLogCounter = 0;
        int callsPerSecond = static_cast<int>(sampleRate) / numFrames;
        debugLogCounter++;
        if (callsPerSecond > 0 && (debugLogCounter % (callsPerSecond * 10)) == 0) {
            double totalPlayed = totalPlayedSeconds_.load();
            spdlog::debug("AudioPlayer::processBlock - pos={:.1f}s, totalPlayed={:.0f}s, limit={:.0f}s",
                          currentSec, totalPlayed, maxSec);
        }
    }

    if (maxSec > 0.0) {
        double totalPlayed = totalPlayedSeconds_.load(std::memory_order_relaxed);
        if (totalPlayed >= maxSec) {
            spdlog::warn("AudioPlayer: TRIAL LIMIT REACHED - totalPlayed={:.0f}s (limit={:.0f}s) - blocking playback",
                         totalPlayed, maxSec);
            std::memset(output, 0, numFrames * channels * sizeof(float));
            state_.store(PlayerState::Stopped);
            stoppedByLimit_.store(true);
            trialExpired_.store(true);
            return;
        }
    }

    double pos = positionFrames_.load(std::memory_order_relaxed);
    double spd = speed_.load(std::memory_order_relaxed);
    float vol = volume_.load(std::memory_order_relaxed);

    // Rampe de volume : sans elle, tout saut de gain produit un clic.
    const float prevVol = previousVolume_.load(std::memory_order_relaxed);
    float volStep = (vol - prevVol) / numFrames;
    float currentVol = prevVol;

    for (int i = 0; i < numFrames; ++i) {
        size_t frame0 = static_cast<size_t>(pos);
        double frac = pos - frame0;

        if (frame0 >= totalFrames) {
            if (looping_.load()) {
                pos = 0.0;
                frame0 = 0;
                frac = 0.0;
            } else {
                std::memset(output + i * channels, 0,
                           (numFrames - i) * channels * sizeof(float));
                state_.store(PlayerState::Stopped);
                break;
            }
        }

        size_t frame1 = std::min(frame0 + 1, totalFrames - 1);
        currentVol += volStep;

        size_t idx0 = frame0 * trackChannels;
        size_t idx1 = frame1 * trackChannels;

        for (int ch = 0; ch < channels; ++ch) {
            int srcCh = (ch < trackChannels) ? ch : (trackChannels - 1);
            float s0 = (idx0 + srcCh < totalSamples) ? rawData[idx0 + srcCh] : 0.0f;
            float s1 = (idx1 + srcCh < totalSamples) ? rawData[idx1 + srcCh] : 0.0f;
            float sample = static_cast<float>(s0 + (s1 - s0) * frac);
            output[i * channels + ch] = sample * currentVol;
        }

        pos += spd;
    }

    previousVolume_.store(vol, std::memory_order_relaxed);
    positionFrames_.store(pos, std::memory_order_relaxed);

    positionCallbackCounter_ += numFrames;
    if (positionCallbackCounter_ >= kPositionCallbackInterval) {
        positionCallbackCounter_ = 0;
        PositionCallback cb;
        {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            cb = positionCallback_;
        }
        if (cb) {
            cb(getPosition());
        }
    }
}

} // namespace BeatMate::Core
