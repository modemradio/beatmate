#include "ThemeManager.h"
#include <spdlog/spdlog.h>

namespace BeatMate::Services::Config {

ThemeManager::ThemeManager() {
    setupDarkTheme(); // Default theme
}

bool ThemeManager::loadTheme(const std::string& name) {
    currentTheme_ = name;

    if (name == "dark") setupDarkTheme();
    else if (name == "light") setupLightTheme();
    else if (name == "midnight") setupMidnightTheme();
    else if (name == "neon") setupNeonTheme();
    else {
        spdlog::warn("ThemeManager: Unknown theme '{}', falling back to dark", name);
        setupDarkTheme();
    }

    spdlog::info("ThemeManager: Loaded theme '{}'", name);
    return true;
}

std::vector<std::string> ThemeManager::getAvailableThemes() const {
    return {"dark", "light", "midnight", "neon"};
}

void ThemeManager::applyToLookAndFeel(juce::LookAndFeel& lf) {
    lf.setColour(juce::ResizableWindow::backgroundColourId, backgroundColour_);
    lf.setColour(juce::Label::textColourId, textColour_);
    lf.setColour(juce::TextEditor::backgroundColourId, backgroundColour_.brighter(0.1f));
    lf.setColour(juce::TextEditor::textColourId, textColour_);
    lf.setColour(juce::TextEditor::outlineColourId, accentColour_.withAlpha(0.5f));
    lf.setColour(juce::TextButton::buttonColourId, buttonColour_);
    lf.setColour(juce::TextButton::buttonOnColourId, accentColour_);
    lf.setColour(juce::TextButton::textColourOnId, textColour_);
    lf.setColour(juce::TextButton::textColourOffId, textColour_);
    lf.setColour(juce::ComboBox::backgroundColourId, buttonColour_);
    lf.setColour(juce::ComboBox::textColourId, textColour_);
    lf.setColour(juce::ComboBox::outlineColourId, accentColour_.withAlpha(0.3f));
    lf.setColour(juce::ListBox::backgroundColourId, backgroundColour_);
    lf.setColour(juce::ListBox::textColourId, textColour_);
    lf.setColour(juce::Slider::backgroundColourId, backgroundColour_.brighter(0.1f));
    lf.setColour(juce::Slider::thumbColourId, accentColour_);
    lf.setColour(juce::Slider::trackColourId, accentColour_.withAlpha(0.6f));
    lf.setColour(juce::ScrollBar::thumbColourId, accentColour_.withAlpha(0.4f));

    spdlog::info("ThemeManager: Applied theme '{}' to LookAndFeel", currentTheme_);
}

void ThemeManager::setupDarkTheme() {
    backgroundColour_ = juce::Colour(0xff1a1a2e);
    textColour_ = juce::Colour(0xffe0e0e0);
    accentColour_ = juce::Colour(0xff0f3460);
    buttonColour_ = juce::Colour(0xff16213e);
    buttonHoverColour_ = juce::Colour(0xff0f3460);
}

void ThemeManager::setupLightTheme() {
    backgroundColour_ = juce::Colour(0xfff5f5f5);
    textColour_ = juce::Colour(0xff1a1a1a);
    accentColour_ = juce::Colour(0xff2196f3);
    buttonColour_ = juce::Colour(0xffe0e0e0);
    buttonHoverColour_ = juce::Colour(0xffbbdefb);
}

void ThemeManager::setupMidnightTheme() {
    backgroundColour_ = juce::Colour(0xff0d0d1a);
    textColour_ = juce::Colour(0xffc0c0d0);
    accentColour_ = juce::Colour(0xff4a148c);
    buttonColour_ = juce::Colour(0xff1a1a33);
    buttonHoverColour_ = juce::Colour(0xff4a148c);
}

void ThemeManager::setupNeonTheme() {
    backgroundColour_ = juce::Colour(0xff0a0a0a);
    textColour_ = juce::Colour(0xff00ff88);
    accentColour_ = juce::Colour(0xffff0066);
    buttonColour_ = juce::Colour(0xff1a1a1a);
    buttonHoverColour_ = juce::Colour(0xff333333);
}

} // namespace BeatMate::Services::Config
