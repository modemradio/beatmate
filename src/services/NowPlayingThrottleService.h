#pragma once
#include "NowPlayingService.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

namespace BeatMate::Services {
class NowPlayingThrottleService {
public:
    using ThrottledCallback = std::function<void(const NowPlayingState&)>;

    NowPlayingThrottleService(NowPlayingService& nowPlaying);
    ~NowPlayingThrottleService();

    void setThrottleIntervalMs(int ms);
    int getThrottleIntervalMs() const { return throttleMs_.load(); }

    void setPositionUpdateIntervalMs(int ms) { positionThrottleMs_ = ms; }
    void setVolumeUpdateIntervalMs(int ms) { volumeThrottleMs_ = ms; }
    void setMetadataUpdateIntervalMs(int ms) { metadataThrottleMs_ = ms; }

    void addThrottledListener(ThrottledCallback callback);

    void forceUpdate();

    void start();
    void stop();
    bool isRunning() const { return running_.load(); }

    int getDroppedUpdateCount() const { return droppedUpdates_.load(); }
    int getDeliveredUpdateCount() const { return deliveredUpdates_.load(); }
    void resetStats();

private:
    void processLoop();

    NowPlayingService& nowPlaying_;
    std::vector<ThrottledCallback> listeners_;
    std::atomic<int> throttleMs_{33};       // ~30fps default
    int positionThrottleMs_ = 33;
    int volumeThrottleMs_ = 50;
    int metadataThrottleMs_ = 500;

    std::atomic<bool> running_{false};
    std::atomic<bool> dirty_{false};
    std::thread processingThread_;
    std::atomic<int> droppedUpdates_{0};
    std::atomic<int> deliveredUpdates_{0};
    mutable std::mutex mutex_;

    NowPlayingState lastDeliveredState_;
    std::chrono::steady_clock::time_point lastPositionUpdate_;
    std::chrono::steady_clock::time_point lastVolumeUpdate_;
    std::chrono::steady_clock::time_point lastMetadataUpdate_;
};

} // namespace BeatMate::Services
