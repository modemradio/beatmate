#pragma once
#include <juce_events/juce_events.h>
#include <string>
#include <functional>
#include <memory>
#include <juce_core/juce_core.h>

namespace BeatMate::Services::Realtime {

using VDJTrackChangedCallback = std::function<void(int deck, const std::string& title, const std::string& artist)>;

class VirtualDJMonitor : private juce::Timer {
public:
    VirtualDJMonitor();
    ~VirtualDJMonitor() override;
    void start(const std::string& ip = "127.0.0.1", int port = 8080);
    void stop();
    void setTrackChangedCallback(VDJTrackChangedCallback cb) { trackChangedCallback_ = std::move(cb); }

private:
    void timerCallback() override;

    std::string ip_;
    int port_ = 8080;
    VDJTrackChangedCallback trackChangedCallback_;
};
} // namespace BeatMate::Services::Realtime
