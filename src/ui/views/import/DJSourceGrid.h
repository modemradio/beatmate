#pragma once
#include <functional>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../IRetranslatable.h"
#include "../../../services/djsoftware/DJSoftwareManager.h"

namespace BeatMate::UI {

class DJSourceGrid : public juce::Component, public IRetranslatable {
public:
    DJSourceGrid() = default;

    void setSources(std::vector<Services::DJSoftware::DJSoftwareInfo> sources);

    std::function<void(Services::DJSoftware::DJSoftwareType)> onSourceChosen;

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void retranslateUi() override { repaint(); }

private:
    juce::Rectangle<int> cardBounds(int index) const;
    int cardAt(juce::Point<int> pos) const;

    std::vector<Services::DJSoftware::DJSoftwareInfo> m_sources;
    int m_hovered = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DJSourceGrid)
};

} // namespace BeatMate::UI
