#pragma once
#include <juce_events/juce_events.h>

#include <string>
#include <functional>
#include <memory>
#include <juce_core/juce_core.h>

namespace BeatMate::Services::Streaming {

class BillboardService;
class BeatportService;

using ChartUpdateCallback = std::function<void(const std::string& chartName, bool success)>;

class ChartScheduler : private juce::Timer {
public:
    ChartScheduler();
    ~ChartScheduler() override;

    void scheduleUpdate(int intervalMinutes = 60);
    void stop();
    void updateCharts();
    bool isRunning() const;

    void setUpdateCallback(ChartUpdateCallback callback) { callback_ = std::move(callback); }
    void setChartsUpdatedCallback(std::function<void()> cb) { chartsUpdatedCallback_ = std::move(cb); }
    void setBillboardService(std::shared_ptr<BillboardService> service) { billboard_ = std::move(service); }
    void setBeatportService(std::shared_ptr<BeatportService> service) { beatport_ = std::move(service); }

private:
    void timerCallback() override;

    std::shared_ptr<BillboardService> billboard_;
    std::shared_ptr<BeatportService> beatport_;
    ChartUpdateCallback callback_;
    std::function<void()> chartsUpdatedCallback_;
    bool running_ = false;
};

}
