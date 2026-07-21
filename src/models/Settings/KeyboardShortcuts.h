#pragma once

#include <string>
#include <map>
#include <vector>
#include <nlohmann/json.hpp>

namespace BeatMate::Models::Settings {

struct ShortcutEntry {
    std::string action;
    std::string keyCombo;
    std::string category;
    std::string description;
    bool isCustom = false;
    bool isEnabled = true;

    ShortcutEntry() = default;

    ShortcutEntry(const std::string& action, const std::string& keyCombo,
                  const std::string& category, const std::string& description)
        : action(action), keyCombo(keyCombo), category(category), description(description) {}

    bool operator==(const ShortcutEntry& other) const { return action == other.action; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ShortcutEntry,
        action, keyCombo, category, description, isCustom, isEnabled
    )
};

struct KeyboardShortcuts {
    // Map: action -> key combo (for quick lookup)
    std::map<std::string, std::string> shortcuts;

    std::vector<ShortcutEntry> entries;

    bool enabled = true;
    bool allowGlobalShortcuts = false;

    KeyboardShortcuts() {
        setDefaults();
    }

    void setDefaults() {
        shortcuts = {
            {"play_pause",          "Space"},
            {"stop",                "Ctrl+Space"},
            {"next_track",          "Ctrl+Right"},
            {"prev_track",          "Ctrl+Left"},
            {"seek_forward",        "Right"},
            {"seek_backward",       "Left"},
            {"seek_forward_big",    "Shift+Right"},
            {"seek_backward_big",   "Shift+Left"},
            {"volume_up",           "Ctrl+Up"},
            {"volume_down",         "Ctrl+Down"},
            {"mute",                "Ctrl+M"},

            {"search",              "Ctrl+F"},
            {"import_files",        "Ctrl+I"},
            {"import_folder",       "Ctrl+Shift+I"},
            {"select_all",          "Ctrl+A"},
            {"deselect_all",        "Ctrl+Shift+A"},
            {"delete",              "Delete"},
            {"rename",              "F2"},
            {"properties",          "Ctrl+P"},

            {"toggle_sidebar",      "Ctrl+B"},
            {"toggle_fullscreen",   "F11"},
            {"zoom_in",             "Ctrl+="},
            {"zoom_out",            "Ctrl+-"},
            {"zoom_reset",          "Ctrl+0"},

            {"load_deck_a",         "Shift+A"},
            {"load_deck_b",         "Shift+B"},
            {"sync_decks",          "Ctrl+S"},
            {"cue_set",             "C"},
            {"cue_play",            "Shift+C"},

            {"hotcue_1",            "1"},
            {"hotcue_2",            "2"},
            {"hotcue_3",            "3"},
            {"hotcue_4",            "4"},
            {"hotcue_5",            "5"},
            {"hotcue_6",            "6"},
            {"hotcue_7",            "7"},
            {"hotcue_8",            "8"},

            {"analyze_selected",    "Ctrl+Shift+A"},
            {"analyze_all",         "Ctrl+Alt+A"},

            {"new_playlist",        "Ctrl+N"},
            {"save_playlist",       "Ctrl+S"},

            {"preferences",         "Ctrl+,"},
            {"undo",                "Ctrl+Z"},
            {"redo",                "Ctrl+Shift+Z"},
            {"quit",                "Ctrl+Q"},
            {"minimize",            "Ctrl+H"},
        };

        entries.clear();
        entries.push_back({"play_pause", "Space", "Playback", "Play / Pause"});
        entries.push_back({"stop", "Ctrl+Space", "Playback", "Stop"});
        entries.push_back({"next_track", "Ctrl+Right", "Playback", "Next Track"});
        entries.push_back({"prev_track", "Ctrl+Left", "Playback", "Previous Track"});
        entries.push_back({"seek_forward", "Right", "Playback", "Seek Forward"});
        entries.push_back({"seek_backward", "Left", "Playback", "Seek Backward"});
        entries.push_back({"volume_up", "Ctrl+Up", "Playback", "Volume Up"});
        entries.push_back({"volume_down", "Ctrl+Down", "Playback", "Volume Down"});
        entries.push_back({"mute", "Ctrl+M", "Playback", "Mute"});
        entries.push_back({"search", "Ctrl+F", "Library", "Search"});
        entries.push_back({"import_files", "Ctrl+I", "Library", "Import Files"});
        entries.push_back({"select_all", "Ctrl+A", "Library", "Select All"});
        entries.push_back({"delete", "Delete", "Library", "Delete Selected"});
        entries.push_back({"properties", "Ctrl+P", "Library", "Track Properties"});
        entries.push_back({"toggle_sidebar", "Ctrl+B", "View", "Toggle Sidebar"});
        entries.push_back({"toggle_fullscreen", "F11", "View", "Toggle Fullscreen"});
        entries.push_back({"load_deck_a", "Shift+A", "Decks", "Load to Deck A"});
        entries.push_back({"load_deck_b", "Shift+B", "Decks", "Load to Deck B"});
        entries.push_back({"new_playlist", "Ctrl+N", "Playlist", "New Playlist"});
        entries.push_back({"preferences", "Ctrl+,", "Application", "Preferences"});
        entries.push_back({"undo", "Ctrl+Z", "Application", "Undo"});
        entries.push_back({"redo", "Ctrl+Shift+Z", "Application", "Redo"});
        entries.push_back({"quit", "Ctrl+Q", "Application", "Quit"});
    }

    [[nodiscard]] std::string getShortcut(const std::string& action) const {
        auto it = shortcuts.find(action);
        return it != shortcuts.end() ? it->second : "";
    }

    void setShortcut(const std::string& action, const std::string& keyCombo) {
        shortcuts[action] = keyCombo;
        for (auto& entry : entries) {
            if (entry.action == action) {
                entry.keyCombo = keyCombo;
                entry.isCustom = true;
                break;
            }
        }
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(KeyboardShortcuts,
        shortcuts, entries, enabled, allowGlobalShortcuts
    )
};

} // namespace BeatMate::Models::Settings
