#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

namespace BeatMate::UI {

class AnimatedProgressBar : public juce::Component,
                            private juce::Timer
{
public:
    AnimatedProgressBar();
    ~AnimatedProgressBar() override;

    void setProgress(float progress);
    float getProgress() const { return progress_; }

    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;

    float progress_ = 0.0f;
    float animationOffset_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnimatedProgressBar)
};

}
