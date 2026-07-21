#include "PerformanceMonitor.h"
#include <spdlog/spdlog.h>
#ifdef _WIN32
#include <Windows.h>
#include <Psapi.h>
#endif
namespace BeatMate::Services::Diagnostics {
PerformanceMonitor::~PerformanceMonitor() { stop(); }
void PerformanceMonitor::start(int sampleIntervalMs) {
    running_ = true;
    thread_ = std::thread(&PerformanceMonitor::sampleLoop, this, sampleIntervalMs);
    spdlog::info("PerformanceMonitor: Started");
}
void PerformanceMonitor::stop() { running_ = false; if (thread_.joinable()) thread_.join(); }
void PerformanceMonitor::sampleLoop(int intervalMs) {
    while (running_) {
#ifdef _WIN32
        MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms); GlobalMemoryStatusEx(&ms);
        ramUsage_ = static_cast<float>(ms.dwMemoryLoad);
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        }
#endif
        if (alertCb_) {
            if (cpuUsage_ > 80.0f) alertCb_("CPU", cpuUsage_);
            if (ramUsage_ > 90.0f) alertCb_("RAM", ramUsage_);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
    }
}
} // namespace BeatMate::Services::Diagnostics
