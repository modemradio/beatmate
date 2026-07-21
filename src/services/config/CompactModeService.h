#pragma once
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace BeatMate::Services::Config {

struct CompactModeSettings {
    bool enabled = false;
    bool hideWaveformOverview = false;
    bool hideBrowserPanel = false;
    bool hideEffectsPanel = false;
    bool hideStemControls = false;
    bool compactMixerView = false;
    bool compactTransportBar = false;
    bool singleDeckMode = false;
    float panelSpacing = 4.0f;
    float normalSpacing = 8.0f;
    int minWindowWidth = 800;
    int minWindowHeight = 500;
};

class CompactModeService {
public:
    using ChangeCallback = std::function<void(bool compactEnabled)>;

    CompactModeService();
    ~CompactModeService() = default;

    void setEnabled(bool enabled);
    bool isEnabled() const;

    CompactModeSettings getSettings() const;
    void setSettings(const CompactModeSettings& settings);

    void setWaveformOverviewVisible(bool visible);
    void setBrowserPanelVisible(bool visible);
    void setEffectsPanelVisible(bool visible);
    void setStemControlsVisible(bool visible);

    bool isWaveformOverviewVisible() const;
    bool isBrowserPanelVisible() const;
    bool isEffectsPanelVisible() const;
    bool isStemControlsVisible() const;

    float getCurrentSpacing() const;
    int getMinimumWidth() const;
    int getMinimumHeight() const;

    void toggleSection(const std::string& sectionId);
    bool isSectionVisible(const std::string& sectionId) const;

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

    void addChangeListener(ChangeCallback callback);

private:
    void notifyChange();
    CompactModeSettings settings_;
    std::vector<ChangeCallback> listeners_;
    mutable std::mutex mutex_;
};

} // namespace BeatMate::Services::Config
