#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <string>
#include <vector>
#include <map>

namespace BeatMate::Services::Config {

class ThemeManager {
public:
    ThemeManager();
    ~ThemeManager() = default;

    bool loadTheme(const std::string& name);
    std::vector<std::string> getAvailableThemes() const;
    std::string getCurrentTheme() const { return currentTheme_; }

    void applyToLookAndFeel(juce::LookAndFeel& lf);

    juce::Colour getBackgroundColour() const { return backgroundColour_; }
    juce::Colour getTextColour() const { return textColour_; }
    juce::Colour getAccentColour() const { return accentColour_; }
    juce::Colour getButtonColour() const { return buttonColour_; }
    juce::Colour getButtonHoverColour() const { return buttonHoverColour_; }

private:
    void setupDarkTheme();
    void setupLightTheme();
    void setupMidnightTheme();
    void setupNeonTheme();

    std::string currentTheme_ = "dark";
    std::string themeDir_ = "themes";

    juce::Colour backgroundColour_;
    juce::Colour textColour_;
    juce::Colour accentColour_;
    juce::Colour buttonColour_;
    juce::Colour buttonHoverColour_;
};

} // namespace BeatMate::Services::Config
