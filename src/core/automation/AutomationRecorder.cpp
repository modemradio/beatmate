#include "AutomationRecorder.h"
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

AutomationRecorder::AutomationRecorder()
    : lane_(std::make_unique<AutomationLane>()) {}

void AutomationRecorder::startRecording(const std::string& parameter) {
    lane_ = std::make_unique<AutomationLane>(parameter);
    startTime_ = std::chrono::steady_clock::now();
    recording_.store(true);
    spdlog::info("AutomationRecorder: recording {}", parameter);
}

void AutomationRecorder::stopRecording() {
    recording_.store(false);
    spdlog::info("AutomationRecorder: stopped ({} points)", lane_->getPoints().size());
}

void AutomationRecorder::recordValue(double value) {
    if (!recording_.load()) return;
    auto now = std::chrono::steady_clock::now();
    double time = std::chrono::duration<double>(now - startTime_).count();
    lane_->addPoint(time, value);
}

}
