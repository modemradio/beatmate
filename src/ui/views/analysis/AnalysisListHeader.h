#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "AnalysisColumns.h"
#include "../../IRetranslatable.h"

namespace BeatMate::UI {

class AnalysisListHeader : public juce::Component, public IRetranslatable
{
public:
    AnalysisListHeader();
    ~AnalysisListHeader() override = default;

    std::function<void(AnalysisColumns::Col)> onSortRequested;

    void setSortState(AnalysisColumns::Col col, bool ascending);
    void retranslateUi() override;

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    AnalysisColumns::Col m_sortColumn = AnalysisColumns::Col::Title;
    bool m_sortAscending = true;
    AnalysisColumns::Col m_hoverColumn = AnalysisColumns::Col::Count;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalysisListHeader)
};

} // namespace BeatMate::UI
