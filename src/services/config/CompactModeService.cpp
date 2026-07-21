#include "CompactModeService.h"
#include <spdlog/spdlog.h>

namespace BeatMate::Services::Config {

CompactModeService::CompactModeService() {}

void CompactModeService::setEnabled(bool enabled) {
    { std::lock_guard<std::mutex> lock(mutex_); settings_.enabled = enabled; }
    spdlog::info("CompactModeService: compact mode {}", enabled ? "enabled" : "disabled");
    notifyChange();
}

bool CompactModeService::isEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_.enabled;
}

CompactModeSettings CompactModeService::getSettings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_;
}

void CompactModeService::setSettings(const CompactModeSettings& settings) {
    { std::lock_guard<std::mutex> lock(mutex_); settings_ = settings; }
    notifyChange();
}

void CompactModeService::setWaveformOverviewVisible(bool v) {
    { std::lock_guard<std::mutex> lock(mutex_); settings_.hideWaveformOverview = !v; }
    notifyChange();
}

void CompactModeService::setBrowserPanelVisible(bool v) {
    { std::lock_guard<std::mutex> lock(mutex_); settings_.hideBrowserPanel = !v; }
    notifyChange();
}

void CompactModeService::setEffectsPanelVisible(bool v) {
    { std::lock_guard<std::mutex> lock(mutex_); settings_.hideEffectsPanel = !v; }
    notifyChange();
}

void CompactModeService::setStemControlsVisible(bool v) {
    { std::lock_guard<std::mutex> lock(mutex_); settings_.hideStemControls = !v; }
    notifyChange();
}

bool CompactModeService::isWaveformOverviewVisible() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !settings_.hideWaveformOverview;
}

bool CompactModeService::isBrowserPanelVisible() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !settings_.hideBrowserPanel;
}

bool CompactModeService::isEffectsPanelVisible() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !settings_.hideEffectsPanel;
}

bool CompactModeService::isStemControlsVisible() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !settings_.hideStemControls;
}

float CompactModeService::getCurrentSpacing() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_.enabled ? settings_.panelSpacing : settings_.normalSpacing;
}

int CompactModeService::getMinimumWidth() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_.minWindowWidth;
}

int CompactModeService::getMinimumHeight() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_.minWindowHeight;
}

void CompactModeService::toggleSection(const std::string& sectionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sectionId == "waveformOverview") settings_.hideWaveformOverview = !settings_.hideWaveformOverview;
    else if (sectionId == "browser") settings_.hideBrowserPanel = !settings_.hideBrowserPanel;
    else if (sectionId == "effects") settings_.hideEffectsPanel = !settings_.hideEffectsPanel;
    else if (sectionId == "stems") settings_.hideStemControls = !settings_.hideStemControls;
    else if (sectionId == "compactMixer") settings_.compactMixerView = !settings_.compactMixerView;
    else if (sectionId == "compactTransport") settings_.compactTransportBar = !settings_.compactTransportBar;
}

bool CompactModeService::isSectionVisible(const std::string& sectionId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sectionId == "waveformOverview") return !settings_.hideWaveformOverview;
    if (sectionId == "browser") return !settings_.hideBrowserPanel;
    if (sectionId == "effects") return !settings_.hideEffectsPanel;
    if (sectionId == "stems") return !settings_.hideStemControls;
    return true;
}

nlohmann::json CompactModeService::toJson() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {
        {"enabled", settings_.enabled},
        {"hideWaveformOverview", settings_.hideWaveformOverview},
        {"hideBrowserPanel", settings_.hideBrowserPanel},
        {"hideEffectsPanel", settings_.hideEffectsPanel},
        {"hideStemControls", settings_.hideStemControls},
        {"compactMixerView", settings_.compactMixerView},
        {"compactTransportBar", settings_.compactTransportBar},
        {"singleDeckMode", settings_.singleDeckMode},
        {"panelSpacing", settings_.panelSpacing},
        {"normalSpacing", settings_.normalSpacing}
    };
}

void CompactModeService::fromJson(const nlohmann::json& j) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_.enabled = j.value("enabled", false);
    settings_.hideWaveformOverview = j.value("hideWaveformOverview", false);
    settings_.hideBrowserPanel = j.value("hideBrowserPanel", false);
    settings_.hideEffectsPanel = j.value("hideEffectsPanel", false);
    settings_.hideStemControls = j.value("hideStemControls", false);
    settings_.compactMixerView = j.value("compactMixerView", false);
    settings_.compactTransportBar = j.value("compactTransportBar", false);
    settings_.singleDeckMode = j.value("singleDeckMode", false);
    settings_.panelSpacing = j.value("panelSpacing", 4.0f);
    settings_.normalSpacing = j.value("normalSpacing", 8.0f);
}

void CompactModeService::addChangeListener(ChangeCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.push_back(std::move(callback));
}

void CompactModeService::notifyChange() {
    bool enabled;
    std::vector<ChangeCallback> cbs;
    { std::lock_guard<std::mutex> lock(mutex_); enabled = settings_.enabled; cbs = listeners_; }
    for (auto& cb : cbs) cb(enabled);
}

}
