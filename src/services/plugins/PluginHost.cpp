#include "PluginHost.h"
#include <spdlog/spdlog.h>
namespace BeatMate::Services::Plugins {
void PluginHost::registerAPI(const std::string& name, std::function<void()> handler) {
    handlers_[name] = std::move(handler);
    spdlog::debug("PluginHost: Registered API '{}'", name);
}
void PluginHost::callAPI(const std::string& name) {
    auto it = handlers_.find(name);
    if (it == handlers_.end() || !it->second) {
        spdlog::warn("PluginHost: API call '{}' has no registered handler", name);
        return;
    }
    spdlog::debug("PluginHost: API call '{}'", name);
    it->second();
}
std::map<std::string, std::string> PluginHost::getHostInfo() const {
    return {{"app", "BeatMate"}, {"version", "12.0"}, {"api_version", "1.0"}};
}
}
