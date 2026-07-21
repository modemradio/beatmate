#pragma once
#include <functional>
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>

namespace BeatMate::UI {

class CueEditPopover : public juce::Component {
public:
    CueEditPopover(int cueIndex, const juce::String& cueName, int colorIndex,
                   bool transientEnabled);

    std::function<void(int, const juce::String&)> onRename;
    std::function<void(int, int)> onColorPicked;
    std::function<void(int)> onSnapTransient;
    std::function<void(int)> onDelete;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;

private:
    juce::Rectangle<int> swatchBounds(int swatchIndex) const;
    void commitRename();

    int m_cueIndex = 0;
    int m_colorIndex = 0;
    int m_hoveredSwatch = -1;
    std::unique_ptr<juce::Label> m_nameLabel;
    std::unique_ptr<juce::TextEditor> m_nameEditor;
    std::unique_ptr<juce::Label> m_colorLabel;
    std::unique_ptr<juce::TextButton> m_transientBtn;
    std::unique_ptr<juce::TextButton> m_deleteBtn;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CueEditPopover)
};

} // namespace BeatMate::UI
