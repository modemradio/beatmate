#include "PluginManager.h"
#include "PluginLoader.h"
#include <spdlog/spdlog.h>
#include <filesystem>
namespace fs = std::filesystem;
namespace BeatMate::Services::Plugins {
PluginManager::~PluginManager() { unloadAll(); }
bool PluginManager::loadPlugins(const std::string& dir) {
    if (!fs::exists(dir)) { spdlog::warn("PluginManager: Plugin directory not found: {}", dir); return false; }
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        if (ext != ".dll" && ext != ".so" && ext != ".dylib") continue;

        auto loader = std::make_unique<PluginLoader>();
        IPlugin* plugin = loader->loadLibrary(entry.path().string());
        if (!plugin) continue;

        LoadedPlugin lp;
        lp.info.name    = plugin->getName();
        lp.info.version = plugin->getVersion();
        lp.info.author  = plugin->getAuthor();
        lp.info.path    = entry.path().string();
        lp.info.enabled = false;
        lp.plugin       = plugin;
        lp.loader       = std::move(loader);
        spdlog::info("PluginManager: Loaded plugin '{}' v{}", lp.info.name, lp.info.version);
        plugins_.push_back(std::move(lp));
    }
    return true;
}
void PluginManager::unloadAll() {
    for (auto& lp : plugins_) {
        if (lp.info.enabled && lp.plugin) lp.plugin->shutdown();
    }
    plugins_.clear();
    spdlog::info("PluginManager: All plugins unloaded");
}
std::vector<PluginInfo> PluginManager::getPlugins() const {
    std::vector<PluginInfo> infos;
    infos.reserve(plugins_.size());
    for (const auto& lp : plugins_) infos.push_back(lp.info);
    return infos;
}
bool PluginManager::enablePlugin(const std::string& name) {
    for (auto& lp : plugins_) {
        if (lp.info.name == name && !lp.info.enabled && lp.plugin) {
            if (lp.plugin->initialize()) { lp.info.enabled = true; return true; }
        }
    }
    return false;
}
bool PluginManager::disablePlugin(const std::string& name) {
    for (auto& lp : plugins_) {
        if (lp.info.name == name && lp.info.enabled && lp.plugin) {
            lp.plugin->shutdown(); lp.info.enabled = false; return true;
        }
    }
    return false;
}
} // namespace BeatMate::Services::Plugins
