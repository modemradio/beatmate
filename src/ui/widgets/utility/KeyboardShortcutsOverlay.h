#pragma once
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class KeyboardShortcutsOverlay : public juce::Component {
public:
    KeyboardShortcutsOverlay(); ~KeyboardShortcutsOverlay() override=default;
    void toggle();void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent&) override{setVisible(false);}
    bool keyPressed(const juce::KeyPress& key) override{if(key==juce::KeyPress::escapeKey){setVisible(false);return true;}return false;}
private:
    struct ShortcutGroup{juce::String title;std::vector<std::pair<juce::String,juce::String>> shortcuts;};
    std::vector<ShortcutGroup> m_groups;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeyboardShortcutsOverlay)
};
} // namespace BeatMate::UI
