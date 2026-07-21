#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

namespace BeatMate::UI {

class CompatibilityBadge : public juce::Component, public juce::SettableTooltipClient
{
public:
    CompatibilityBadge();
    ~CompatibilityBadge() override = default;

    void setCompatibility(float score, const juce::String& details = {});
    float getScore() const { return score_; }

    void paint(juce::Graphics& g) override;

private:
    float score_ = 0.0f;          // 0.0 - 1.0
    juce::String details_;
    juce::String label_;
    juce::Colour badgeColour_;

    void updateAppearance();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompatibilityBadge)
};

} // namespace BeatMate::UI
