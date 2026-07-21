#include "NowPlayingThrottleService.h"
#include <spdlog/spdlog.h>

namespace BeatMate::Services {

NowPlayingThrottleService::NowPlayingThrottleService(NowPlayingService& nowPlaying)
    : nowPlaying_(nowPlaying)
{
    auto now = std::chrono::steady_clock::now();
    lastPositionUpdate_ = now;
    lastVolumeUpdate_ = now;
    lastMetadataUpdate_ = now;

    nowPlaying_.addStateChangeListener([this](const NowPlayingState&) {
        dirty_.store(true);
    });
}

NowPlayingThrottleService::~NowPlayingThrottleService() {
    stop();
}

void NowPlayingThrottleService::setThrottleIntervalMs(int ms) {
    throttleMs_.store(std::max(1, ms));
}

void NowPlayingThrottleService::addThrottledListener(ThrottledCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.push_back(std::move(callback));
}

void NowPlayingThrottleService::forceUpdate() {
    dirty_.store(true);
}

void NowPlayingThrottleService::start() {
    if (running_.load()) return;
    running_.store(true);
    processingThread_ = std::thread(&NowPlayingThrottleService::processLoop, this);
    spdlog::info("NowPlayingThrottleService: started with {}ms throttle", throttleMs_.load());
}

void NowPlayingThrottleService::stop() {
    running_.store(false);
    if (processingThread_.joinable()) processingThread_.join();
}

void NowPlayingThrottleService::resetStats() {
    droppedUpdates_.store(0);
    deliveredUpdates_.store(0);
}

void NowPlayingThrottleService::processLoop() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(throttleMs_.load()));

        if (!dirty_.load()) continue;
        dirty_.store(false);

        auto currentState = nowPlaying_.getState();
        auto now = std::chrono::steady_clock::now();

        bool hasPositionChange = false;
        bool hasVolumeChange = false;
        bool hasMetadataChange = false;

        for (int d = 0; d < 4; ++d) {
            const auto& cur = (d == 0) ? currentState.deck1 : (d == 1) ? currentState.deck2 : (d == 2) ? currentState.deck3 : currentState.deck4;
            const auto& last = (d == 0) ? lastDeliveredState_.deck1 : (d == 1) ? lastDeliveredState_.deck2 : (d == 2) ? lastDeliveredState_.deck3 : lastDeliveredState_.deck4;

            if (std::abs(cur.positionSeconds - last.positionSeconds) > 0.01) hasPositionChange = true;
            if (std::abs(cur.volume - last.volume) > 0.001f) hasVolumeChange = true;
            if (cur.trackId != last.trackId || cur.isPlaying != last.isPlaying || cur.title != last.title) hasMetadataChange = true;
        }

        bool shouldDeliver = false;
        if (hasMetadataChange) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMetadataUpdate_).count();
            if (elapsed >= metadataThrottleMs_) { shouldDeliver = true; lastMetadataUpdate_ = now; }
        }
        if (hasVolumeChange) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastVolumeUpdate_).count();
            if (elapsed >= volumeThrottleMs_) { shouldDeliver = true; lastVolumeUpdate_ = now; }
        }
        if (hasPositionChange) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPositionUpdate_).count();
            if (elapsed >= positionThrottleMs_) { shouldDeliver = true; lastPositionUpdate_ = now; }
        }

        if (hasMetadataChange) shouldDeliver = true;

        if (shouldDeliver) {
            lastDeliveredState_ = currentState;
            deliveredUpdates_.fetch_add(1);

            std::vector<ThrottledCallback> cbs;
            { std::lock_guard<std::mutex> lock(mutex_); cbs = listeners_; }
            for (auto& cb : cbs) {
                try { cb(currentState); }
                catch (...) {}
            }
        } else {
            droppedUpdates_.fetch_add(1);
        }
    }
}

} // namespace BeatMate::Services
