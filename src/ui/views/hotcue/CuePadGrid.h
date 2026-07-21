#pragma once
#include <array>
#include <functional>
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include "CuePadComponent.h"

namespace BeatMate::UI {

class CuePadGrid : public juce::Component {
public:
    CuePadGrid();

    void refreshPad(int index, const PadInfo& info);
    void setSelectedIndex(int index);
    void setHoldIndex(int index);
    void setPreviewingIndex(int index);
    int selectedIndex() const { return m_selectedIndex; }

    std::function<void(int)> onPadPress;
    std::function<void(int)> onPadRelease;
    std::function<void(int)> onPadContextMenu;

    juce::Rectangle<int> padScreenBounds(int index) const;

    void resized() override;

private:
    std::array<std::unique_ptr<CuePadComponent>, 8> m_pads;
    int m_selectedIndex = -1;
    int m_holdIndex = -1;
    int m_previewingIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CuePadGrid)
};

}
