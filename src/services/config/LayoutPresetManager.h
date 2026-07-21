#pragma once
#include "WorkspaceService.h"
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace BeatMate::Services::Config {

struct LayoutPreset {
    std::string id;
    std::string name;
    std::string category;
    int shortcutKey = 0;
    WorkspaceLayout layout;
    int64_t lastUsed = 0;
    int useCount = 0;
};

class LayoutPresetManager {
public:
    using PresetChangeCallback = std::function<void(const LayoutPreset&)>;

    LayoutPresetManager();
    ~LayoutPresetManager() = default;

    void addPreset(const LayoutPreset& preset);
    bool removePreset(const std::string& id);
    bool updatePreset(const std::string& id, const LayoutPreset& preset);
    void clearUserPresets();

    std::vector<LayoutPreset> getAllPresets() const;
    std::vector<LayoutPreset> getUserPresets() const;
    std::vector<LayoutPreset> getRecentPresets(int maxCount = 5) const;
    const LayoutPreset* getPreset(const std::string& id) const;
    const LayoutPreset* getPresetByShortcut(int key) const;

    void applyPreset(const std::string& id);
    LayoutPreset getCurrentPreset() const;

    void nextPreset();
    void previousPreset();

    void assignShortcut(const std::string& presetId, int key);
    void clearShortcut(const std::string& presetId);

    LayoutPreset captureCurrentAsPreset(const std::string& name,
                                         const WorkspaceLayout& currentLayout);

    bool loadFromFile(const std::string& path);
    bool saveToFile(const std::string& path);

    void addChangeListener(PresetChangeCallback callback);

private:
    void notifyChange();
    int64_t currentTimestamp() const;

    std::vector<LayoutPreset> presets_;
    std::string currentPresetId_;
    int currentIndex_ = 0;
    std::vector<PresetChangeCallback> listeners_;
    mutable std::mutex mutex_;
};

}
