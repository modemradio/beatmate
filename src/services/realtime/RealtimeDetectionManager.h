#pragma once
#include <memory>
#include <functional>
#include <string>
#include "../../models/Track.h"

namespace BeatMate::Services::Realtime {

class RekordboxMonitor;
class VirtualDJMonitor;
class RealtimeCoordinator;

using TrackChangeCallback = std::function<void(const Models::Track&)>;
using TrackDetectedCallback = std::function<void(const std::string& title, const std::string& artist)>;

class RealtimeDetectionManager {
public:
    RealtimeDetectionManager();
    ~RealtimeDetectionManager();
    void start();
    void stop();
    bool isRunning() const { return running_; }
    void setTrackChangeCallback(TrackChangeCallback cb) { callback_ = std::move(cb); }
    void setTrackDetectedCallback(TrackDetectedCallback cb) { trackDetectedCallback_ = std::move(cb); }

private:
    bool running_ = false;
    TrackChangeCallback callback_;
    TrackDetectedCallback trackDetectedCallback_;

    std::unique_ptr<RekordboxMonitor> rekordboxMonitor_;
    std::unique_ptr<VirtualDJMonitor> virtualDJMonitor_;
    std::unique_ptr<RealtimeCoordinator> coordinator_;
};
} // namespace BeatMate::Services::Realtime
