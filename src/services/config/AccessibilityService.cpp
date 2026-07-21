#include "AccessibilityService.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>

namespace BeatMate::Services::Config {

AccessibilityService::AccessibilityService() {}

AccessibilitySettings AccessibilityService::getSettings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_;
}

void AccessibilityService::setSettings(const AccessibilitySettings& settings) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        settings_ = settings;
    }
    notifyChange();
    spdlog::info("AccessibilityService: settings updated (reduceMotion={}, highContrast={}, fontScale={:.1f}, uiScale={:.1f})",
                 settings.reduceMotion, settings.highContrast, settings.fontScale, settings.uiScale);
}

void AccessibilityService::setReduceMotion(bool enabled) {
    { std::lock_guard<std::mutex> lock(mutex_); settings_.reduceMotion = enabled; }
    notifyChange();
}

bool AccessibilityService::isReduceMotionEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_.reduceMotion;
}

void AccessibilityService::setHighContrast(bool enabled) {
    { std::lock_guard<std::mutex> lock(mutex_); settings_.highContrast = enabled; }
    notifyChange();
}

bool AccessibilityService::isHighContrastEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_.highContrast;
}

void AccessibilityService::setLargeFonts(bool enabled) {
    { std::lock_guard<std::mutex> lock(mutex_); settings_.largeFonts = enabled; settings_.fontScale = enabled ? 1.5f : 1.0f; }
    notifyChange();
}

bool AccessibilityService::isLargeFontsEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_.largeFonts;
}

void AccessibilityService::setFontScale(float scale) {
    { std::lock_guard<std::mutex> lock(mutex_); settings_.fontScale = std::clamp(scale, 0.5f, 3.0f); }
    notifyChange();
}

float AccessibilityService::getFontScale() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_.fontScale;
}

void AccessibilityService::setColorBlindMode(int mode) {
    { std::lock_guard<std::mutex> lock(mutex_); settings_.colorBlindMode = std::clamp(mode, 0, 3); settings_.colorBlindAssist = (mode > 0); }
    notifyChange();
}

int AccessibilityService::getColorBlindMode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_.colorBlindMode;
}

void AccessibilityService::setUIScale(float scale) {
    { std::lock_guard<std::mutex> lock(mutex_); settings_.uiScale = std::clamp(scale, 0.5f, 3.0f); }
    notifyChange();
}

float AccessibilityService::getUIScale() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_.uiScale;
}

void AccessibilityService::setAnimationSpeed(float speed) {
    { std::lock_guard<std::mutex> lock(mutex_); settings_.animationSpeed = std::clamp(speed, 0.0f, 2.0f); }
    notifyChange();
}

float AccessibilityService::getAnimationSpeed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_.animationSpeed;
}

float AccessibilityService::getEffectiveFontSize(float baseFontSize) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return baseFontSize * settings_.fontScale * settings_.uiScale;
}

int AccessibilityService::getEffectiveAnimationDurationMs(int baseDurationMs) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (settings_.reduceMotion) return 0;
    return static_cast<int>(baseDurationMs / std::max(0.01f, settings_.animationSpeed));
}

bool AccessibilityService::shouldAnimate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !settings_.reduceMotion && settings_.animationSpeed > 0.01f;
}

nlohmann::json AccessibilityService::toJson() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {
        {"reduceMotion", settings_.reduceMotion},
        {"highContrast", settings_.highContrast},
        {"largeFonts", settings_.largeFonts},
        {"fontScale", settings_.fontScale},
        {"screenReaderHints", settings_.screenReaderHints},
        {"flashingDisabled", settings_.flashingDisabled},
        {"colorBlindAssist", settings_.colorBlindAssist},
        {"colorBlindMode", settings_.colorBlindMode},
        {"keyboardNavigation", settings_.keyboardNavigation},
        {"animationSpeed", settings_.animationSpeed},
        {"hapticFeedback", settings_.hapticFeedback},
        {"uiScale", settings_.uiScale}
    };
}

void AccessibilityService::fromJson(const nlohmann::json& j) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_.reduceMotion = j.value("reduceMotion", false);
    settings_.highContrast = j.value("highContrast", false);
    settings_.largeFonts = j.value("largeFonts", false);
    settings_.fontScale = j.value("fontScale", 1.0f);
    settings_.screenReaderHints = j.value("screenReaderHints", false);
    settings_.flashingDisabled = j.value("flashingDisabled", false);
    settings_.colorBlindAssist = j.value("colorBlindAssist", false);
    settings_.colorBlindMode = j.value("colorBlindMode", 0);
    settings_.keyboardNavigation = j.value("keyboardNavigation", true);
    settings_.animationSpeed = j.value("animationSpeed", 1.0f);
    settings_.hapticFeedback = j.value("hapticFeedback", false);
    settings_.uiScale = j.value("uiScale", 1.0f);
}

void AccessibilityService::addChangeListener(ChangeCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.push_back(std::move(callback));
}

void AccessibilityService::notifyChange() {
    AccessibilitySettings s;
    std::vector<ChangeCallback> cbs;
    { std::lock_guard<std::mutex> lock(mutex_); s = settings_; cbs = listeners_; }
    for (auto& cb : cbs) cb(s);
}

}
