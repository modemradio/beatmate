#include "SystemInfo.h"
#include <spdlog/spdlog.h>
#include <filesystem>

namespace BeatMate::Services::Diagnostics {

std::string SystemInfo::getCPU() {
    auto cpuModel = juce::SystemStats::getCpuModel().toStdString();
    int numCores = juce::SystemStats::getNumCpus();
    int speedMHz = juce::SystemStats::getCpuSpeedInMegahertz();

    if (!cpuModel.empty()) {
        return cpuModel + " (" + std::to_string(numCores) + " cores, " +
               std::to_string(speedMHz) + " MHz)";
    }
    return "CPU: " + std::to_string(numCores) + " cores, " +
           std::to_string(speedMHz) + " MHz";
}

std::string SystemInfo::getRAM() {
    int ramMB = juce::SystemStats::getMemorySizeInMegabytes();
    if (ramMB > 0) {
        return std::to_string(ramMB / 1024) + " GB (" + std::to_string(ramMB) + " MB)";
    }
    return "Unknown";
}

std::string SystemInfo::getOS() {
    return juce::SystemStats::getOperatingSystemName().toStdString();
}

std::string SystemInfo::getGPU() {
    // Services layer should not depend on JUCE GUI modules
    return "Unknown";
}

std::vector<std::string> SystemInfo::getAudioDevices() {
    // Services layer should not instantiate AudioDeviceManager directly.
    std::vector<std::string> devices;
    devices.push_back("Default Audio Device");
    return devices;
}

std::string SystemInfo::getDiskSpace() {
    try {
        auto space = std::filesystem::space(".");
        return std::to_string(space.available / (1024 * 1024 * 1024)) + " GB free";
    } catch (...) {
        return "Unknown";
    }
}

} // namespace BeatMate::Services::Diagnostics
