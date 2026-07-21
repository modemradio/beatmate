#pragma once
#include <string>
#include "IPlugin.h"
namespace BeatMate::Services::Plugins {
// Ordre de destruction : liberer l'IPlugin* avant de fermer le handle DLL/.so
class PluginLoader {
public:
    PluginLoader() = default;
    ~PluginLoader();

    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;
    PluginLoader(PluginLoader&& other) noexcept;
    PluginLoader& operator=(PluginLoader&& other) noexcept;

    IPlugin* loadLibrary(const std::string& path);

    void unload();

private:
    void* handle_ = nullptr;
    IPlugin* plugin_ = nullptr;
};
} // namespace BeatMate::Services::Plugins
