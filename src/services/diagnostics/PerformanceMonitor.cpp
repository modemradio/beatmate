#include "PerformanceMonitor.h"
#include <spdlog/spdlog.h>
#ifdef _WIN32
#include <Windows.h>
#include <Psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <sys/sysctl.h>
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
#elif defined(__APPLE__)
        uint64_t memSize = 0;
        size_t memLen = sizeof(memSize);
        if (sysctlbyname("hw.memsize", &memSize, &memLen, nullptr, 0) == 0 && memSize > 0) {
            vm_statistics64_data_t vmStat;
            mach_msg_type_number_t vmCount = HOST_VM_INFO64_COUNT;
            vm_size_t pageSize = 0;
            host_page_size(mach_host_self(), &pageSize);
            if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                                  reinterpret_cast<host_info64_t>(&vmStat), &vmCount) == KERN_SUCCESS) {
                const uint64_t usedBytes = ((uint64_t) vmStat.active_count
                                          + vmStat.wire_count
                                          + vmStat.compressor_page_count)
                                          * (uint64_t) pageSize;
                ramUsage_ = static_cast<float>((double) usedBytes * 100.0 / (double) memSize);
            }
        }
        static uint64_t s_prevBusy = 0, s_prevIdle = 0;
        host_cpu_load_info_data_t cpuLoad;
        mach_msg_type_number_t cpuCount = HOST_CPU_LOAD_INFO_COUNT;
        if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                            reinterpret_cast<host_info_t>(&cpuLoad), &cpuCount) == KERN_SUCCESS) {
            const uint64_t busy = (uint64_t) cpuLoad.cpu_ticks[CPU_STATE_USER]
                                + cpuLoad.cpu_ticks[CPU_STATE_SYSTEM]
                                + cpuLoad.cpu_ticks[CPU_STATE_NICE];
            const uint64_t idle = cpuLoad.cpu_ticks[CPU_STATE_IDLE];
            const uint64_t dBusy = busy - s_prevBusy;
            const uint64_t dIdle = idle - s_prevIdle;
            const uint64_t total = dBusy + dIdle;
            if (total > 0 && s_prevBusy != 0)
                cpuUsage_ = static_cast<float>((double) dBusy * 100.0 / (double) total);
            s_prevBusy = busy;
            s_prevIdle = idle;
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
