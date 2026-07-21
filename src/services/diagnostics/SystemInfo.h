#pragma once
#include <juce_core/juce_core.h>
#include <string>
#include <vector>

namespace BeatMate::Services::Diagnostics {

class SystemInfo {
public:
    SystemInfo() = default;

    std::string getCPU();
    std::string getRAM();
    std::string getOS();
    std::string getGPU();
    std::vector<std::string> getAudioDevices();
    std::string getDiskSpace();
};

} // namespace BeatMate::Services::Diagnostics
