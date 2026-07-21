#pragma once

#ifndef _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING
#define _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING
#endif

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace BeatMate::Core {

class AudioTrack;

enum class PlayerState { Stopped, Playing, Paused };

using PositionCallback = std::function<void(double positionSeconds)>;

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    void loadTrack(std::shared_ptr<AudioTrack> track);
    std::shared_ptr<AudioTrack> getTrack() const { return std::atomic_load(&track_); }

    void play();
    void pause();
    void stop();
    void seek(double seconds);

    void setPosition(double seconds);
    double getPosition() const;
    double getDuration() const;

    void setVolume(float linearVolume); // 0.0 - 1.0
    float getVolume() const { return volume_.load(); }

    void setSpeed(double speed); // 1.0 = normal
    double getSpeed() const { return speed_.load(); }

    void setPitch(double semitones); // -12 to +12
    double getPitch() const { return pitch_.load(); }

    bool isPlaying() const { return state_.load() == PlayerState::Playing; }
    bool isPaused() const { return state_.load() == PlayerState::Paused; }
    bool isStopped() const { return state_.load() == PlayerState::Stopped; }
    PlayerState getState() const { return state_.load(); }

    void setPositionCallback(PositionCallback cb);

    void processBlock(float* output, int numFrames, int channels);

    void setLooping(bool loop) { looping_.store(loop); }
    bool isLooping() const { return looping_.load(); }

    void setMaxPlaybackSeconds(double maxSec) { maxPlaybackSeconds_.store(maxSec); }
    double getMaxPlaybackSeconds() const { return maxPlaybackSeconds_.load(); }
    bool wasStoppedByLimit() const { return stoppedByLimit_.load(); }
    void clearStoppedByLimit() { stoppedByLimit_.store(false); }
    double getTotalPlayedSeconds() const { return totalPlayedSeconds_.load(); }
    bool isTrialExpired() const { return trialExpired_.load(); }
    void resetTrialState() {
        trialExpired_.store(false);
        stoppedByLimit_.store(false);
        totalPlayedSeconds_.store(0.0);
    }

private:
    std::shared_ptr<AudioTrack> track_;
    std::atomic<PlayerState> state_{PlayerState::Stopped};
    std::atomic<double> positionFrames_{0.0};
    std::atomic<float> volume_{1.0f};
    std::atomic<double> speed_{1.0};
    std::atomic<double> pitch_{0.0};
    std::atomic<bool> looping_{false};
    std::atomic<double> maxPlaybackSeconds_{0.0}; // 0 = unlimited
    std::atomic<bool> stoppedByLimit_{false};
    std::atomic<double> totalPlayedSeconds_{0.0}; // cumulative across all tracks
    std::atomic<bool> trialExpired_{false};        // permanent until license activated

    std::atomic<float> previousVolume_{1.0f};

    PositionCallback positionCallback_;
    std::mutex callbackMutex_;

    int positionCallbackCounter_ = 0;
    static constexpr int kPositionCallbackInterval = 2048; // samples between callbacks
};

} // namespace BeatMate::Core
