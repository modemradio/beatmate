#include "RealtimeCoordinator.h"
#include "RealtimeDetectionManager.h"
#include "RekordboxMonitor.h"
#include "VirtualDJMonitor.h"
#include <spdlog/spdlog.h>

namespace BeatMate::Services::Realtime {

RealtimeCoordinator::RealtimeCoordinator() {}
RealtimeCoordinator::~RealtimeCoordinator() { stop(); }

void RealtimeCoordinator::start(int pollIntervalMs) {
    startTimer(pollIntervalMs);
    running_ = true;
    spdlog::info("RealtimeCoordinator: Started (interval={}ms)", pollIntervalMs);
}

void RealtimeCoordinator::stop() {
    stopTimer();
    running_ = false;
    spdlog::info("RealtimeCoordinator: Stopped");
}

void RealtimeCoordinator::timerCallback() {
    spdlog::trace("RealtimeCoordinator: heartbeat");
}

} // namespace BeatMate::Services::Realtime
