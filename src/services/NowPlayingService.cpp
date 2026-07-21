#include <algorithm>
#include "NowPlayingService.h"
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Services {

NowPlayingService::NowPlayingService() {
    state_.deck1.deckIndex = 0;
    state_.deck2.deckIndex = 1;
    state_.deck3.deckIndex = 2;
    state_.deck4.deckIndex = 3;
}

NowPlayingState NowPlayingService::getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

NowPlayingInfo NowPlayingService::getDeckInfo(int deckIndex) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return getDeckConstRef(deckIndex);
}

void NowPlayingService::updateDeck(int deckIndex, const NowPlayingInfo& info) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        getDeckRef(deckIndex) = info;
        getDeckRef(deckIndex).deckIndex = deckIndex;
    }
    notifyListeners();
}

void NowPlayingService::updateDeckPosition(int deckIndex, double positionSeconds) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        getDeckRef(deckIndex).positionSeconds = positionSeconds;
    }
    notifyListeners();
}

void NowPlayingService::updateDeckVolume(int deckIndex, float volume) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        getDeckRef(deckIndex).volume = volume;
    }
    notifyListeners();
}

void NowPlayingService::setDeckPlaying(int deckIndex, bool playing) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        getDeckRef(deckIndex).isPlaying = playing;
    }
    notifyListeners();
}

void NowPlayingService::setMasterDeck(int deckIndex) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.masterDeck = deckIndex;
        state_.deck1.isMaster = (deckIndex == 0);
        state_.deck2.isMaster = (deckIndex == 1);
        state_.deck3.isMaster = (deckIndex == 2);
        state_.deck4.isMaster = (deckIndex == 3);
    }
    notifyListeners();
}

void NowPlayingService::setMasterBpm(double bpm) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.masterBpm = bpm;
    }
    notifyListeners();
}

void NowPlayingService::setCrossfaderPosition(float position) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.crossfaderPosition = position;
    }
    notifyListeners();
}

void NowPlayingService::setRecording(bool recording, double elapsed) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.isRecording = recording;
        state_.recordingElapsed = elapsed;
    }
    notifyListeners();
}

void NowPlayingService::setActiveDeckCount(int count) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.activeDeckCount = std::clamp(count, 2, 4);
}

void NowPlayingService::addStateChangeListener(StateChangeCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.push_back(std::move(callback));
}

NowPlayingInfo NowPlayingService::getMasterDeckInfo() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return getDeckConstRef(state_.masterDeck);
}

bool NowPlayingService::isAnyDeckPlaying() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_.deck1.isPlaying || state_.deck2.isPlaying ||
           state_.deck3.isPlaying || state_.deck4.isPlaying;
}

std::string NowPlayingService::getFormattedPosition(int deckIndex) const {
    std::lock_guard<std::mutex> lock(mutex_);
    double secs = getDeckConstRef(deckIndex).positionSeconds;
    int mins = static_cast<int>(secs) / 60;
    int s = static_cast<int>(secs) % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", mins, s);
    return buf;
}

std::string NowPlayingService::getFormattedRemaining(int deckIndex) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& info = getDeckConstRef(deckIndex);
    double remaining = std::max(0.0, info.durationSeconds - info.positionSeconds);
    int mins = static_cast<int>(remaining) / 60;
    int s = static_cast<int>(remaining) % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "-%d:%02d", mins, s);
    return buf;
}

float NowPlayingService::getProgressPercent(int deckIndex) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& info = getDeckConstRef(deckIndex);
    if (info.durationSeconds <= 0.0) return 0.0f;
    return static_cast<float>(info.positionSeconds / info.durationSeconds);
}

void NowPlayingService::notifyListeners() {
    NowPlayingState s;
    std::vector<StateChangeCallback> cbs;
    { std::lock_guard<std::mutex> lock(mutex_); s = state_; cbs = listeners_; }
    for (auto& cb : cbs) cb(s);
}

NowPlayingInfo& NowPlayingService::getDeckRef(int deckIndex) {
    switch (deckIndex) {
        case 0: return state_.deck1;
        case 1: return state_.deck2;
        case 2: return state_.deck3;
        case 3: return state_.deck4;
        default: return state_.deck1;
    }
}

const NowPlayingInfo& NowPlayingService::getDeckConstRef(int deckIndex) const {
    switch (deckIndex) {
        case 0: return state_.deck1;
        case 1: return state_.deck2;
        case 2: return state_.deck3;
        case 3: return state_.deck4;
        default: return state_.deck1;
    }
}

} // namespace BeatMate::Services
