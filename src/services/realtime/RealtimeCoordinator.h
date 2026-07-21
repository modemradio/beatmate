#pragma once
#include <juce_events/juce_events.h>
#include <vector>
#include <memory>
#include <juce_core/juce_core.h>

namespace BeatMate::Services::Realtime {
class RealtimeDetectionManager;

class RealtimeCoordinator : private juce::Timer {
public:
    RealtimeCoordinator();
    ~RealtimeCoordinator() override;
    void start(int pollIntervalMs = 1000);
    void stop();
    bool isRunning() const { return running_; }

private:
    void timerCallback() override;
    bool running_ = false;
};
} // namespace BeatMate::Services::Realtime
