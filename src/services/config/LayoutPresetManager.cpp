#include "LayoutPresetManager.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>

namespace BeatMate::Services::Config {

LayoutPresetManager::LayoutPresetManager() {}

void LayoutPresetManager::addPreset(const LayoutPreset& preset) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(presets_.begin(), presets_.end(),
        [&](const LayoutPreset& p) { return p.id == preset.id; });
    if (it != presets_.end()) *it = preset;
    else presets_.push_back(preset);
    spdlog::info("LayoutPresetManager: added preset '{}'", preset.name);
}

bool LayoutPresetManager::removePreset(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(presets_.begin(), presets_.end(),
        [&](const LayoutPreset& p) { return p.id == id; });
    if (it == presets_.end() || it->category == "factory") return false;
    presets_.erase(it);
    return true;
}

bool LayoutPresetManager::updatePreset(const std::string& id, const LayoutPreset& preset) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& p : presets_) {
        if (p.id == id) { p = preset; return true; }
    }
    return false;
}

void LayoutPresetManager::clearUserPresets() {
    std::lock_guard<std::mutex> lock(mutex_);
    presets_.erase(std::remove_if(presets_.begin(), presets_.end(),
        [](const LayoutPreset& p) { return p.category == "user"; }), presets_.end());
}

std::vector<LayoutPreset> LayoutPresetManager::getAllPresets() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return presets_;
}

std::vector<LayoutPreset> LayoutPresetManager::getUserPresets() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LayoutPreset> result;
    for (auto& p : presets_) if (p.category == "user") result.push_back(p);
    return result;
}

std::vector<LayoutPreset> LayoutPresetManager::getRecentPresets(int maxCount) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto sorted = presets_;
    std::sort(sorted.begin(), sorted.end(),
        [](const LayoutPreset& a, const LayoutPreset& b) { return a.lastUsed > b.lastUsed; });
    if ((int)sorted.size() > maxCount) sorted.resize(maxCount);
    return sorted;
}

const LayoutPreset* LayoutPresetManager::getPreset(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& p : presets_) if (p.id == id) return &p;
    return nullptr;
}

const LayoutPreset* LayoutPresetManager::getPresetByShortcut(int key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& p : presets_) if (p.shortcutKey == key && key != 0) return &p;
    return nullptr;
}

void LayoutPresetManager::applyPreset(const std::string& id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int i = 0; i < (int)presets_.size(); ++i) {
            if (presets_[i].id == id) {
                currentPresetId_ = id;
                currentIndex_ = i;
                presets_[i].lastUsed = currentTimestamp();
                presets_[i].useCount++;
                spdlog::info("LayoutPresetManager: applied preset '{}'", presets_[i].name);
                break;
            }
        }
    }
    notifyChange();
}

LayoutPreset LayoutPresetManager::getCurrentPreset() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& p : presets_) if (p.id == currentPresetId_) return p;
    if (!presets_.empty()) return presets_.front();
    return {};
}

void LayoutPresetManager::nextPreset() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (presets_.empty()) return;
        currentIndex_ = (currentIndex_ + 1) % (int)presets_.size();
        currentPresetId_ = presets_[currentIndex_].id;
        presets_[currentIndex_].lastUsed = currentTimestamp();
    }
    notifyChange();
}

void LayoutPresetManager::previousPreset() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (presets_.empty()) return;
        currentIndex_ = (currentIndex_ - 1 + (int)presets_.size()) % (int)presets_.size();
        currentPresetId_ = presets_[currentIndex_].id;
        presets_[currentIndex_].lastUsed = currentTimestamp();
    }
    notifyChange();
}

void LayoutPresetManager::assignShortcut(const std::string& presetId, int key) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Clear key from any other preset
    for (auto& p : presets_) if (p.shortcutKey == key) p.shortcutKey = 0;
    for (auto& p : presets_) if (p.id == presetId) { p.shortcutKey = key; break; }
}

void LayoutPresetManager::clearShortcut(const std::string& presetId) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& p : presets_) if (p.id == presetId) { p.shortcutKey = 0; break; }
}

LayoutPreset LayoutPresetManager::captureCurrentAsPreset(const std::string& name,
                                                          const WorkspaceLayout& currentLayout) {
    LayoutPreset preset;
    preset.id = "user_" + std::to_string(currentTimestamp());
    preset.name = name;
    preset.category = "user";
    preset.layout = currentLayout;
    preset.lastUsed = currentTimestamp();
    preset.useCount = 0;
    return preset;
}

bool LayoutPresetManager::loadFromFile(const std::string& path) {
    try {
        if (!std::filesystem::exists(path)) return false;
        std::ifstream ifs(path);
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        auto json = nlohmann::json::parse(content);
        std::lock_guard<std::mutex> lock(mutex_);
        if (json.contains("presets") && json["presets"].is_array()) {
            for (auto& pj : json["presets"]) {
                LayoutPreset p;
                p.id = pj.value("id", "");
                p.name = pj.value("name", "");
                p.category = pj.value("category", "user");
                p.shortcutKey = pj.value("shortcutKey", 0);
                p.lastUsed = pj.value("lastUsed", (int64_t)0);
                p.useCount = pj.value("useCount", 0);
                if (pj.contains("layout")) {
                    WorkspaceService ws;
                    p.layout = ws.fromJson(pj["layout"]);
                }
                if (!p.id.empty()) presets_.push_back(p);
            }
        }
        currentPresetId_ = json.value("currentPresetId", "");
        return true;
    } catch (...) { return false; }
}

bool LayoutPresetManager::saveToFile(const std::string& path) {
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        nlohmann::json json;
        json["currentPresetId"] = currentPresetId_;
        nlohmann::json arr = nlohmann::json::array();
        WorkspaceService ws;
        for (auto& p : presets_) {
            nlohmann::json pj;
            pj["id"] = p.id; pj["name"] = p.name; pj["category"] = p.category;
            pj["shortcutKey"] = p.shortcutKey; pj["lastUsed"] = p.lastUsed; pj["useCount"] = p.useCount;
            pj["layout"] = ws.toJson(p.layout);
            arr.push_back(pj);
        }
        json["presets"] = arr;
        std::ofstream ofs(path);
        if (!ofs) return false;
        ofs << json.dump(2);
        return true;
    } catch (...) { return false; }
}

void LayoutPresetManager::addChangeListener(PresetChangeCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.push_back(std::move(callback));
}

void LayoutPresetManager::notifyChange() {
    LayoutPreset current;
    std::vector<PresetChangeCallback> cbs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& p : presets_) if (p.id == currentPresetId_) { current = p; break; }
        cbs = listeners_;
    }
    for (auto& cb : cbs) cb(current);
}

int64_t LayoutPresetManager::currentTimestamp() const {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace BeatMate::Services::Config
