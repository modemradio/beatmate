#include "SettingsManager.h"
#include "../../app/ServiceLocator.h"
#include "../persistence/SettingsStore.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <filesystem>

namespace BeatMate { extern ServiceLocator* g_serviceLocator; }

namespace BeatMate::Services::Config {
bool SettingsManager::load(const std::string& path) {
    filePath_ = path.empty() ? "settings.json" : path;

    if (BeatMate::g_serviceLocator) {
        if (auto* store = BeatMate::g_serviceLocator->tryGet<Services::Persistence::SettingsStore>()) {
            if (auto blob = store->getKV("settings.json"); blob.has_value()) {
                try {
                    std::lock_guard<std::mutex> lock(mutex_);
                    settings_ = nlohmann::json::parse(*blob);
                    spdlog::info("SettingsManager: Loaded from SettingsStore");
                    return true;
                } catch (const std::exception& e) {
                    spdlog::warn("SettingsManager: DB blob invalid ({}), falling back to file", e.what());
                }
            }
        }
    }

    if (!std::filesystem::exists(filePath_)) { spdlog::info("SettingsManager: No settings file, using defaults"); return true; }
    std::ifstream f(filePath_);
    if (!f.is_open()) return false;
    try { std::lock_guard<std::mutex> lock(mutex_); settings_ = nlohmann::json::parse(f); }
    catch (const std::exception& e) { spdlog::error("SettingsManager: Parse error: {}", e.what()); return false; }
    spdlog::info("SettingsManager: Loaded from {}", filePath_);
    return true;
}
bool SettingsManager::save(const std::string& path) {
    std::string savePath = path.empty() ? filePath_ : path;

    std::string dumped;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dumped = settings_.dump(2);
    }

    bool dbOk = false;
    if (BeatMate::g_serviceLocator) {
        if (auto* store = BeatMate::g_serviceLocator->tryGet<Services::Persistence::SettingsStore>()) {
            dbOk = store->setKV("settings.json", dumped);
        }
    }

    // Sans chemin fourni la DB suffit ; écriture via .tmp puis rename pour éviter la corruption en cas de crash
    bool fileOk = false;
    if (!savePath.empty()) {
        const std::string tmpPath = savePath + ".tmp";
        bool wrote = false;
        {
            std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
            if (f.is_open()) {
                f << dumped;
                f.flush();
                wrote = f.good();
            }
        }
        if (wrote) {
            std::error_code ec;
            std::filesystem::rename(tmpPath, savePath, ec);
            if (!ec) {
                fileOk = true;
            } else {
                spdlog::error("SettingsManager: rename {} -> {} failed: {}", tmpPath, savePath, ec.message());
                std::error_code rmEc;
                std::filesystem::remove(tmpPath, rmEc);
            }
        } else {
            spdlog::error("SettingsManager: Cannot save to {}", tmpPath);
            std::error_code rmEc;
            std::filesystem::remove(tmpPath, rmEc);
        }
    }

    return (dbOk || fileOk);
}

bool SettingsManager::saveDB() {
    if (!BeatMate::g_serviceLocator) return false;
    auto* store = BeatMate::g_serviceLocator->tryGet<Services::Persistence::SettingsStore>();
    if (!store) return false;

    std::string dumped;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dumped = settings_.dump(2);
    }
    return store->setKV("settings.json", dumped);
}
} // namespace BeatMate::Services::Config
