#include "KeyboardShortcutsManager.h"
#include "../../app/ServiceLocator.h"
#include "../persistence/SettingsStore.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>

namespace BeatMate { extern ServiceLocator* g_serviceLocator; }

namespace BeatMate::Services::Config {

KeyboardShortcutsManager::KeyboardShortcutsManager() {
    for (auto& [action, combo] : shortcuts_.shortcuts) {
        keyPresses_[action] = parseKeyCombo(combo);
    }
}

bool KeyboardShortcutsManager::loadFromJson(const nlohmann::json& j) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        shortcuts_ = j.get<Models::Settings::KeyboardShortcuts>();
        keyPresses_.clear();
        for (auto& [action, combo] : shortcuts_.shortcuts) {
            keyPresses_[action] = parseKeyCombo(combo);
        }
        spdlog::info("KeyboardShortcutsManager: loaded {} shortcuts", shortcuts_.shortcuts.size());
        return true;
    } catch (...) { return false; }
}

nlohmann::json KeyboardShortcutsManager::toJson() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return shortcuts_;
}

bool KeyboardShortcutsManager::loadFromFile(const std::string& path) {
    // DB-first: SettingsStore holds the authoritative blob once migrated.
    if (BeatMate::g_serviceLocator) {
        if (auto* store = BeatMate::g_serviceLocator->tryGet<Services::Persistence::SettingsStore>()) {
            if (auto blob = store->getKV("shortcuts.json"); blob.has_value()) {
                try {
                    auto j = nlohmann::json::parse(*blob);
                    if (loadFromJson(j)) return true;
                } catch (...) {}
            }
        }
    }
    // Fallback: legacy file on disk.
    try {
        if (!std::filesystem::exists(path)) return false;
        std::ifstream ifs(path);
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        auto j = nlohmann::json::parse(content);
        return loadFromJson(j);
    } catch (...) { return false; }
}

bool KeyboardShortcutsManager::saveToFile(const std::string& path) {
    // Serialize current state outside the lock so we can call into the store
    std::string dumped;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dumped = nlohmann::json(shortcuts_).dump(2);
    }

    bool dbOk = false;
    if (BeatMate::g_serviceLocator) {
        if (auto* store = BeatMate::g_serviceLocator->tryGet<Services::Persistence::SettingsStore>()) {
            dbOk = store->setKV("shortcuts.json", dumped);
        }
    }
    bool fileOk = false;
    try {
        std::ofstream ofs(path);
        if (ofs) { ofs << dumped; fileOk = true; }
    } catch (...) {}

    if (!dbOk && !fileOk)
        spdlog::warn("[KeyboardShortcuts] saveToFile: both DB and file writes failed");
    return dbOk || fileOk;
}

void KeyboardShortcutsManager::registerAction(const std::string& actionId, ActionCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    actions_[actionId] = std::move(callback);
}

void KeyboardShortcutsManager::unregisterAction(const std::string& actionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    actions_.erase(actionId);
}

void KeyboardShortcutsManager::setShortcut(const std::string& actionId, const juce::KeyPress& keyPress) {
    std::lock_guard<std::mutex> lock(mutex_);
    keyPresses_[actionId] = keyPress;
    std::string combo = keyPressToString(keyPress);
    shortcuts_.setShortcut(actionId, combo);
}

void KeyboardShortcutsManager::setShortcut(const std::string& actionId, const std::string& keyCombo) {
    std::lock_guard<std::mutex> lock(mutex_);
    keyPresses_[actionId] = parseKeyCombo(keyCombo);
    shortcuts_.setShortcut(actionId, keyCombo);
}

void KeyboardShortcutsManager::removeShortcut(const std::string& actionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    keyPresses_.erase(actionId);
    shortcuts_.shortcuts.erase(actionId);
}

juce::KeyPress KeyboardShortcutsManager::getShortcut(const std::string& actionId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = keyPresses_.find(actionId);
    return it != keyPresses_.end() ? it->second : juce::KeyPress();
}

std::string KeyboardShortcutsManager::getShortcutString(const std::string& actionId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return shortcuts_.getShortcut(actionId);
}

bool KeyboardShortcutsManager::hasConflict(const std::string& actionId, const juce::KeyPress& keyPress) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, kp] : keyPresses_) {
        if (id != actionId && kp == keyPress) return true;
    }
    return false;
}

std::string KeyboardShortcutsManager::getConflictingAction(const juce::KeyPress& keyPress) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, kp] : keyPresses_) {
        if (kp == keyPress) return id;
    }
    return "";
}

std::vector<Models::Settings::ShortcutEntry> KeyboardShortcutsManager::getAllShortcuts() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return shortcuts_.entries;
}

std::vector<Models::Settings::ShortcutEntry> KeyboardShortcutsManager::getShortcutsByCategory(const std::string& category) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Models::Settings::ShortcutEntry> result;
    for (auto& e : shortcuts_.entries)
        if (e.category == category) result.push_back(e);
    return result;
}

std::vector<std::string> KeyboardShortcutsManager::getCategories() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> cats;
    for (auto& e : shortcuts_.entries) {
        if (std::find(cats.begin(), cats.end(), e.category) == cats.end())
            cats.push_back(e.category);
    }
    return cats;
}

void KeyboardShortcutsManager::resetToDefaults() {
    std::lock_guard<std::mutex> lock(mutex_);
    shortcuts_.setDefaults();
    keyPresses_.clear();
    for (auto& [action, combo] : shortcuts_.shortcuts) {
        keyPresses_[action] = parseKeyCombo(combo);
    }
    spdlog::info("KeyboardShortcutsManager: reset to defaults");
}

void KeyboardShortcutsManager::resetAction(const std::string& actionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    Models::Settings::KeyboardShortcuts defaults;
    auto it = defaults.shortcuts.find(actionId);
    if (it != defaults.shortcuts.end()) {
        shortcuts_.setShortcut(actionId, it->second);
        keyPresses_[actionId] = parseKeyCombo(it->second);
    }
}

void KeyboardShortcutsManager::setActionEnabled(const std::string& actionId, bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& e : shortcuts_.entries) {
        if (e.action == actionId) { e.isEnabled = enabled; break; }
    }
}

bool KeyboardShortcutsManager::isActionEnabled(const std::string& actionId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& e : shortcuts_.entries)
        if (e.action == actionId) return e.isEnabled;
    return true;
}

bool KeyboardShortcutsManager::keyPressed(const juce::KeyPress& key, juce::Component*) {
    if (!enabled_) return false;

    std::string matchedAction;
    ActionCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [actionId, kp] : keyPresses_) {
            if (kp == key) {
                bool actionEnabled = true;
                for (auto& e : shortcuts_.entries)
                    if (e.action == actionId) { actionEnabled = e.isEnabled; break; }
                if (!actionEnabled) continue;

                auto it = actions_.find(actionId);
                if (it != actions_.end()) {
                    matchedAction = actionId;
                    callback = it->second;
                    break;
                }
            }
        }
    }

    if (callback) {
        callback();
        return true;
    }
    return false;
}

juce::KeyPress KeyboardShortcutsManager::parseKeyCombo(const std::string& combo) {
    juce::String juceCombo(combo);
    int modifiers = 0;
    juce::String keyPart = juceCombo;

    if (juceCombo.containsIgnoreCase("Ctrl+") || juceCombo.containsIgnoreCase("Cmd+")) {
        modifiers |= juce::ModifierKeys::ctrlModifier;
        keyPart = keyPart.replace("Ctrl+", "", true).replace("Cmd+", "", true);
    }
    if (juceCombo.containsIgnoreCase("Shift+")) {
        modifiers |= juce::ModifierKeys::shiftModifier;
        keyPart = keyPart.replace("Shift+", "", true);
    }
    if (juceCombo.containsIgnoreCase("Alt+")) {
        modifiers |= juce::ModifierKeys::altModifier;
        keyPart = keyPart.replace("Alt+", "", true);
    }

    keyPart = keyPart.trim();
    int keyCode = 0;

    if (keyPart.equalsIgnoreCase("Space")) keyCode = juce::KeyPress::spaceKey;
    else if (keyPart.equalsIgnoreCase("Enter") || keyPart.equalsIgnoreCase("Return")) keyCode = juce::KeyPress::returnKey;
    else if (keyPart.equalsIgnoreCase("Escape") || keyPart.equalsIgnoreCase("Esc")) keyCode = juce::KeyPress::escapeKey;
    else if (keyPart.equalsIgnoreCase("Tab")) keyCode = juce::KeyPress::tabKey;
    else if (keyPart.equalsIgnoreCase("Delete") || keyPart.equalsIgnoreCase("Del")) keyCode = juce::KeyPress::deleteKey;
    else if (keyPart.equalsIgnoreCase("Backspace")) keyCode = juce::KeyPress::backspaceKey;
    else if (keyPart.equalsIgnoreCase("Up")) keyCode = juce::KeyPress::upKey;
    else if (keyPart.equalsIgnoreCase("Down")) keyCode = juce::KeyPress::downKey;
    else if (keyPart.equalsIgnoreCase("Left")) keyCode = juce::KeyPress::leftKey;
    else if (keyPart.equalsIgnoreCase("Right")) keyCode = juce::KeyPress::rightKey;
    else if (keyPart.equalsIgnoreCase("Home")) keyCode = juce::KeyPress::homeKey;
    else if (keyPart.equalsIgnoreCase("End")) keyCode = juce::KeyPress::endKey;
    else if (keyPart.equalsIgnoreCase("PageUp")) keyCode = juce::KeyPress::pageUpKey;
    else if (keyPart.equalsIgnoreCase("PageDown")) keyCode = juce::KeyPress::pageDownKey;
    else if (keyPart.length() >= 2 && keyPart[0] == 'F') {
        int fNum = keyPart.substring(1).getIntValue();
        if (fNum >= 1 && fNum <= 12) keyCode = juce::KeyPress::F1Key + fNum - 1;
    }
    else if (keyPart.length() == 1) keyCode = keyPart.toUpperCase()[0];
    else keyCode = keyPart.toUpperCase()[0];

    return juce::KeyPress(keyCode, juce::ModifierKeys(modifiers), 0);
}

std::string KeyboardShortcutsManager::keyPressToString(const juce::KeyPress& key) {
    std::string result;
    auto mods = key.getModifiers();
    if (mods.isCtrlDown()) result += "Ctrl+";
    if (mods.isShiftDown()) result += "Shift+";
    if (mods.isAltDown()) result += "Alt+";

    int code = key.getKeyCode();
    if (code == juce::KeyPress::spaceKey) result += "Space";
    else if (code == juce::KeyPress::returnKey) result += "Enter";
    else if (code == juce::KeyPress::escapeKey) result += "Escape";
    else if (code == juce::KeyPress::tabKey) result += "Tab";
    else if (code == juce::KeyPress::deleteKey) result += "Delete";
    else if (code == juce::KeyPress::backspaceKey) result += "Backspace";
    else if (code == juce::KeyPress::upKey) result += "Up";
    else if (code == juce::KeyPress::downKey) result += "Down";
    else if (code == juce::KeyPress::leftKey) result += "Left";
    else if (code == juce::KeyPress::rightKey) result += "Right";
    else if (code >= juce::KeyPress::F1Key && code <= juce::KeyPress::F12Key)
        result += "F" + std::to_string(code - juce::KeyPress::F1Key + 1);
    else result += std::string(1, (char)std::toupper(code));

    return result;
}

} // namespace BeatMate::Services::Config
