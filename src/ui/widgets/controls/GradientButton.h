#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

namespace BeatMate::UI {

// GradientButton - Button with blue-to-violet gradient on hover and glow
class GradientButton : public juce::Component
{
public:
    GradientButton(const juce::String& text = {});
    ~GradientButton() override = default;

    void setText(const juce::String& text);
    void setEnabled(bool enabled);

    void paint(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;

    std::function<void()> onClick;

private:
    juce::String text_;
    bool hovered_ = false;
    bool pressed_ = false;
    bool enabled_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GradientButton)
};

} // namespace BeatMate::UI
