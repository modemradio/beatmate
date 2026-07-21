#pragma once
#include <string>
#include <vector>
namespace BeatMate::Services::Plugins {
class PluginSandbox {
public:
    PluginSandbox() = default;
    void setAllowedPaths(const std::vector<std::string>& paths) { allowedPaths_ = paths; }
    bool isPathAllowed(const std::string& path) const;
    void setMaxMemoryMB(int mb) { maxMemoryMB_ = mb; }
    void setMaxCPUPercent(int percent) { maxCPU_ = percent; }
private:
    std::vector<std::string> allowedPaths_;
    int maxMemoryMB_ = 256;
    int maxCPU_ = 25;
};
}
