#pragma once
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace BeatMate::Services::Config {


struct AccessibilitySettings {
    bool reduceMotion = false;          // Disable animations
    bool highContrast = false;          // High contrast mode
    bool largeFonts = false;            // Larger text
    float fontScale = 1.0f;             // Font scale factor (1.0 = normal, 1.5 = 150%)
    bool screenReaderHints = false;     // Extra hints for screen readers
    bool flashingDisabled = false;      // Disable flashing elements
    bool colorBlindAssist = false;      // Color-blind friendly palette
    int colorBlindMode = 0;             // 0=none, 1=protanopia, 2=deuteranopia, 3=tritanopia
    bool keyboardNavigation = true;     // Full keyboard navigation
    float animationSpeed = 1.0f;        // Animation speed multiplier (0=instant)
    bool hapticFeedback = false;        // Haptic feedback if available
    float uiScale = 1.0f;              // Global UI scale
};

class AccessibilityService {
public:
    using ChangeCallback = std::function<void(const AccessibilitySettings&)>;

    AccessibilityService();
    ~AccessibilityService() = default;

    AccessibilitySettings getSettings() const;
    void setSettings(const AccessibilitySettings& settings);

    void setReduceMotion(bool enabled);
    bool isReduceMotionEnabled() const;

    void setHighContrast(bool enabled);
    bool isHighContrastEnabled() const;

    void setLargeFonts(bool enabled);
    bool isLargeFontsEnabled() const;

    void setFontScale(float scale);
    float getFontScale() const;

    void setColorBlindMode(int mode);
    int getColorBlindMode() const;

    void setUIScale(float scale);
    float getUIScale() const;

    void setAnimationSpeed(float speed);
    float getAnimationSpeed() const;

    float getEffectiveFontSize(float baseFontSize) const;
    int getEffectiveAnimationDurationMs(int baseDurationMs) const;
    bool shouldAnimate() const;

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

    void addChangeListener(ChangeCallback callback);

private:
    void notifyChange();

    AccessibilitySettings settings_;
    std::vector<ChangeCallback> listeners_;
    mutable std::mutex mutex_;
};

} // namespace BeatMate::Services::Config
