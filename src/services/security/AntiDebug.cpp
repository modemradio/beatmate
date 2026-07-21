#include "AntiDebug.h"
#include <spdlog/spdlog.h>
#ifdef _WIN32
#include <Windows.h>
#endif
namespace BeatMate::Services::Security {
AntiDebug::~AntiDebug() { stop(); }
void AntiDebug::start() {
    running_ = true;
    thread_ = std::thread(&AntiDebug::monitorThread, this);
    spdlog::info("AntiDebug: Monitoring started");
}
void AntiDebug::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}
bool AntiDebug::isDebuggerPresent() const {
#ifdef _WIN32
    if (::IsDebuggerPresent()) return true;
    BOOL remoteDebugger = FALSE;
    ::CheckRemoteDebuggerPresent(GetCurrentProcess(), &remoteDebugger);
    return remoteDebugger != FALSE;
#else
    return false;
#endif
}
void AntiDebug::monitorThread() {
    int detectionCount = 0;
    while (running_) {
        if (isDebuggerPresent()) {
            detectionCount++;
            spdlog::warn("AntiDebug: Debugger detected! (count={})", detectionCount);
            if (detectionCount >= 3) {
                // After 3 detections (15 seconds), invalidate license state
                spdlog::error("AntiDebug: Persistent debugger - disabling features");
                if (onDebuggerDetected_) onDebuggerDetected_();
            }
        } else {
            detectionCount = 0;
        }
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}
} // namespace BeatMate::Services::Security
