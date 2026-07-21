#pragma once
#include <string>
#include <map>
#include <functional>
namespace BeatMate::Services::Plugins {
class PluginHost {
public:
    PluginHost() = default;
    void registerAPI(const std::string& name, std::function<void()> handler);
    void callAPI(const std::string& name);
    std::map<std::string, std::string> getHostInfo() const;

private:
    std::map<std::string, std::function<void()>> handlers_;
};
} // namespace BeatMate::Services::Plugins
