#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace BeatMate::UI {

struct PadInfo {
    bool active = false;
    juce::Colour color;
    juce::String name;
    juce::String timeText;
};

class CuePadComponent : public juce::Component {
public:
    explicit CuePadComponent(int index);

    void setInfo(const PadInfo& info);
    void setSelected(bool selected);
    void setHolding(bool holding);
    void setPreviewing(bool previewing);

    std::function<void(int)> onPress;
    std::function<void(int)> onRelease;
    std::function<void(int)> onContextMenu;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
    int m_index = 0;
    PadInfo m_info;
    bool m_selected = false;
    bool m_holding = false;
    bool m_previewing = false;
    bool m_hovered = false;
    bool m_pressed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CuePadComponent)
};

} // namespace BeatMate::UI
