#pragma once
#include <atomic>
#include <thread>
#include <functional>
namespace BeatMate::Services::Security {
class AntiDebug {
public:
    AntiDebug() = default;
    ~AntiDebug();
    void start();
    void stop();
    bool isDebuggerPresent() const;
    void setOnDebuggerDetected(std::function<void()> cb) { onDebuggerDetected_ = std::move(cb); }
private:
    void monitorThread();
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::function<void()> onDebuggerDetected_;
};
} // namespace BeatMate::Services::Security
