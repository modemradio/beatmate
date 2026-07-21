#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "IPlugin.h"
#include "PluginLoader.h"
namespace BeatMate::Services::Plugins {
struct PluginInfo { std::string name; std::string version; std::string author; std::string path; bool enabled = false; };
class PluginManager {
public:
    PluginManager() = default;
    ~PluginManager();
    bool loadPlugins(const std::string& dir);
    void unloadAll();
    std::vector<PluginInfo> getPlugins() const;
    bool enablePlugin(const std::string& name);
    bool disablePlugin(const std::string& name);
private:
    struct LoadedPlugin {
        PluginInfo info;
        std::unique_ptr<PluginLoader> loader;
        IPlugin* plugin = nullptr;
    };
    std::vector<LoadedPlugin> plugins_;
};
} // namespace BeatMate::Services::Plugins
