#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace BeatMate::UI {

class BentoGrid : public juce::Component
{
public:
    BentoGrid();
    ~BentoGrid() override = default;

    void addCard(juce::Component* card, int colSpan = 1, int rowSpan = 1);
    void removeAllCards();
    void setGap(int gap);
    void setColumns(int numColumns);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct CardEntry
    {
        juce::Component* component = nullptr;
        int colSpan = 1;
        int rowSpan = 1;
    };

    std::vector<CardEntry> cards_;
    int gap_ = 12;
    int numColumns_ = 3;

    void layoutCards();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BentoGrid)
};

} // namespace BeatMate::UI
