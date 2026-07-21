#pragma once
#include "../../models/Settings/KeyboardShortcuts.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace BeatMate::Services::Config {

class KeyboardShortcutsManager : public juce::KeyListener {
public:
    using ActionCallback = std::function<void()>;

    KeyboardShortcutsManager();
    ~KeyboardShortcutsManager() override = default;

    bool loadFromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
    bool loadFromFile(const std::string& path);
    bool saveToFile(const std::string& path);

    void registerAction(const std::string& actionId, ActionCallback callback);
    void unregisterAction(const std::string& actionId);

    void setShortcut(const std::string& actionId, const juce::KeyPress& keyPress);
    void setShortcut(const std::string& actionId, const std::string& keyCombo);
    void removeShortcut(const std::string& actionId);
    juce::KeyPress getShortcut(const std::string& actionId) const;
    std::string getShortcutString(const std::string& actionId) const;

    bool hasConflict(const std::string& actionId, const juce::KeyPress& keyPress) const;
    std::string getConflictingAction(const juce::KeyPress& keyPress) const;

    std::vector<Models::Settings::ShortcutEntry> getAllShortcuts() const;
    std::vector<Models::Settings::ShortcutEntry> getShortcutsByCategory(const std::string& category) const;
    std::vector<std::string> getCategories() const;
    bool isEnabled() const { return enabled_; }
    void setEnabled(bool enabled) { enabled_ = enabled; }

    void resetToDefaults();
    void resetAction(const std::string& actionId);

    void setActionEnabled(const std::string& actionId, bool enabled);
    bool isActionEnabled(const std::string& actionId) const;

    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;

    static juce::KeyPress parseKeyCombo(const std::string& combo);
    static std::string keyPressToString(const juce::KeyPress& key);

private:
    Models::Settings::KeyboardShortcuts shortcuts_;
    std::map<std::string, ActionCallback> actions_;
    std::map<std::string, juce::KeyPress> keyPresses_;
    bool enabled_ = true;
    mutable std::mutex mutex_;
};

} // namespace BeatMate::Services::Config
