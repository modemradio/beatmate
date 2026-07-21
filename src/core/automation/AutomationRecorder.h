#pragma once
#include "AutomationLane.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
namespace BeatMate::Core {
class AutomationRecorder {
public:
    AutomationRecorder();
    void startRecording(const std::string& parameter);
    void stopRecording();
    void recordValue(double value);
    bool isRecording() const { return recording_.load(); }
    const AutomationLane& getRecordedLane() const { return *lane_; }
private:
    std::unique_ptr<AutomationLane> lane_;
    std::atomic<bool> recording_{false};
    std::chrono::steady_clock::time_point startTime_;
};
} // namespace BeatMate::Core
