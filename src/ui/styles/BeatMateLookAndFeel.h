#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ThemeEngine.h"

namespace BeatMate::UI {

class BeatMateLookAndFeel : public juce::LookAndFeel_V4,
                            private ThemeEngine::Listener
{
public:
    BeatMateLookAndFeel();
    ~BeatMateLookAndFeel() override;

    void refreshColours();

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;
    void drawScrollbar(juce::Graphics& g, juce::ScrollBar& scrollbar,
                       int x, int y, int width, int height,
                       bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                       bool isMouseOver, bool isMouseDown) override;
    void drawTableHeaderBackground(juce::Graphics& g, juce::TableHeaderComponent& header) override;
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override;
    void drawComboBox(juce::Graphics& g, int width, int height,
                      bool isButtonDown, int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;
    void drawProgressBar(juce::Graphics& g, juce::ProgressBar& bar,
                         int width, int height, double progress,
                         const juce::String& textToShow) override;
    void drawTabButton(juce::TabBarButton& button, juce::Graphics& g,
                       bool isMouseOver, bool isMouseDown) override;
    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                           bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override;
    void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override;
    void drawAlertBox(juce::Graphics& g, juce::AlertWindow& alert,
                      const juce::Rectangle<int>& textArea, juce::TextLayout& textLayout) override;
    void drawGroupComponentOutline(juce::Graphics& g, int width, int height,
                                   const juce::String& text, const juce::Justification& position,
                                   juce::GroupComponent& group) override;
    void drawCallOutBoxBackground(juce::CallOutBox& box, juce::Graphics& g,
                                  const juce::Path& path, juce::Image& cachedImage) override;

    int getDefaultScrollbarWidth() override;
    int getMinimumScrollbarThumbSize(juce::ScrollBar&) override;

private:
    void themeChanged(ThemeEngine::Theme theme) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BeatMateLookAndFeel)
};

} // namespace BeatMate::UI
