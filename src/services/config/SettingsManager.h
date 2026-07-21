#pragma once
#include <string>
#include <map>
#include <functional>
#include <mutex>
#include <any>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
namespace BeatMate::Services::Config {
class SettingsManager {
public:
    SettingsManager() = default;
    bool load(const std::string& path = "");
    bool save(const std::string& path = "");
    // Fast persistence path: writes only to SettingsStore (DB), no file flush.
    bool saveDB();
    template<typename T> T get(const std::string& key, const T& defaultVal = T{}) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (settings_.contains(key)) {
            try { return settings_[key].get<T>(); }
            catch (const std::exception& e) {
                spdlog::warn("SettingsManager::get('{}'): type mismatch ({}), returning default", key, e.what());
            }
        }
        return defaultVal;
    }
    template<typename T> void set(const std::string& key, const T& value) {
        std::function<void()> cb;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            settings_[key] = value;
            auto it = changeCallbacks_.find(key);
            if (it != changeCallbacks_.end()) cb = it->second;
        }
        if (cb) cb();
    }
    void onChange(const std::string& key, std::function<void()> callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        changeCallbacks_[key] = std::move(callback);
    }
    // Copie thread-safe du JSON sous-jacent.
    nlohmann::json snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return settings_;
    }
private:
    nlohmann::json settings_;
    std::string filePath_;
    std::map<std::string, std::function<void()>> changeCallbacks_;
    mutable std::mutex mutex_;
};
} // namespace BeatMate::Services::Config
