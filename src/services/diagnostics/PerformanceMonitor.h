#pragma once
#include <string>
#include <functional>
#include <atomic>
#include <thread>
namespace BeatMate::Services::Diagnostics {
class PerformanceMonitor {
public:
    PerformanceMonitor() = default;
    ~PerformanceMonitor();
    void start(int sampleIntervalMs = 1000);
    void stop();
    float getCPUUsage() const { return cpuUsage_; }
    float getRAMUsage() const { return ramUsage_; }
    float getAudioLatency() const { return audioLatency_; }
    void setThresholdCallback(std::function<void(const std::string&, float)> cb) { alertCb_ = std::move(cb); }
private:
    void sampleLoop(int intervalMs);
    std::atomic<float> cpuUsage_{0}, ramUsage_{0}, audioLatency_{0};
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::function<void(const std::string&, float)> alertCb_;
};
} // namespace BeatMate::Services::Diagnostics
