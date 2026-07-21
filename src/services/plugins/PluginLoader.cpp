#include "PluginLoader.h"
#include <spdlog/spdlog.h>
#include <utility>
#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif
namespace BeatMate::Services::Plugins {

PluginLoader::~PluginLoader() { unload(); }

PluginLoader::PluginLoader(PluginLoader&& other) noexcept
    : handle_(other.handle_), plugin_(other.plugin_) {
    other.handle_ = nullptr;
    other.plugin_ = nullptr;
}

PluginLoader& PluginLoader::operator=(PluginLoader&& other) noexcept {
    if (this != &other) {
        unload();
        handle_ = other.handle_;
        plugin_ = other.plugin_;
        other.handle_ = nullptr;
        other.plugin_ = nullptr;
    }
    return *this;
}

IPlugin* PluginLoader::loadLibrary(const std::string& path) {
    using CreateFunc = IPlugin*(*)();

    // Refuse to load twice into the same loader; that would leak the previous
    if (handle_ || plugin_) {
        spdlog::error("PluginLoader: loader already holds a plugin; refusing to reload");
        return nullptr;
    }

    void* rawHandle = nullptr;
    CreateFunc create = nullptr;
#ifdef _WIN32
    HMODULE lib = LoadLibraryA(path.c_str());
    if (!lib) {
        spdlog::error("PluginLoader: Failed to load {}: error {}", path, GetLastError());
        return nullptr;
    }
    rawHandle = lib;
    create = reinterpret_cast<CreateFunc>(GetProcAddress(lib, "createPlugin"));
#else
    void* lib = dlopen(path.c_str(), RTLD_LAZY);
    if (!lib) {
        spdlog::error("PluginLoader: Failed to load {}: {}", path, dlerror());
        return nullptr;
    }
    rawHandle = lib;
    create = reinterpret_cast<CreateFunc>(dlsym(lib, "createPlugin"));
#endif
    if (!create) {
        spdlog::error("PluginLoader: No createPlugin function in {}", path);
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(rawHandle));
#else
        dlclose(rawHandle);
#endif
        return nullptr;
    }

    IPlugin* plugin = create();
    if (!plugin) {
        spdlog::error("PluginLoader: createPlugin() returned nullptr in {}", path);
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(rawHandle));
#else
        dlclose(rawHandle);
#endif
        return nullptr;
    }

    // Commit: keep the handle alive for the lifetime of the plugin.
    handle_ = rawHandle;
    plugin_ = plugin;
    spdlog::info("PluginLoader: Loaded plugin from {}", path);
    return plugin;
}

void PluginLoader::unload() {
    // CRITICAL: delete the IPlugin instance *before* freeing the library.
    if (plugin_) {
        delete plugin_;
        plugin_ = nullptr;
    }
    if (handle_) {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle_));
#else
        dlclose(handle_);
#endif
        handle_ = nullptr;
    }
}

} // namespace BeatMate::Services::Plugins
