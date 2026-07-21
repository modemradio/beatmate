#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>

#include "../../../models/HandoverPoint.h"

namespace BeatMate::UI::Widgets {

class HandoverListWidget : public juce::Component
{
public:
    HandoverListWidget();
    ~HandoverListWidget() override = default;

    void setHandovers(const std::vector<Models::HandoverPoint>& points);
    std::vector<Models::HandoverPoint> getHandovers() const;

    std::function<void()> onChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct Row : public juce::Component
    {
        Row();
        void paint(juce::Graphics& g) override;
        void resized() override;

        juce::Label        timeLabel { {}, "Time (min):" };
        juce::TextEditor   timeEditor;
        juce::Label        fromLabel { {}, "From:" };
        juce::TextEditor   fromEditor;
        juce::Label        toLabel   { {}, "To:" };
        juce::TextEditor   toEditor;
        juce::Label        notesLabel{ {}, "Notes:" };
        juce::TextEditor   notesEditor;
        juce::TextButton   removeButton { "x" };

        std::function<void()> onFieldChanged;
        std::function<void()> onRemoveClicked;
    };

    void rebuildRows();
    void addHandover();
    void removeAt(int index);
    void pullFromRow(int index);
    void notifyChanged();

    static std::string makeSimpleId();

    std::vector<Models::HandoverPoint>   handovers;
    std::vector<std::unique_ptr<Row>>    rows;
    juce::TextButton                     addButton { "+ Add handover" };

    static constexpr int kRowHeight    = 42;
    static constexpr int kRowGap       = 4;
    static constexpr int kHeaderHeight = 28;
    static constexpr int kFooterHeight = 36;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HandoverListWidget)
};

} // namespace BeatMate::UI::Widgets
