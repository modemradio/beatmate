#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <map>

namespace BeatMate::UI {

class ThemeEngine
{
public:
    enum Theme { Dark, Light, Nord, Dracula, HighContrast, Custom };

    static ThemeEngine& instance();

    void setTheme(Theme theme);
    Theme currentTheme() const { return m_currentTheme; }

    bool loadCustomTheme(const juce::String& path);
    void reloadPersisted();
    bool saveCustomTheme(const juce::String& path) const;
    static juce::File defaultCustomThemeFile();

    void applyTheme(juce::LookAndFeel& lf);

    juce::Colour getColor(const juce::String& key) const;
    void setColor(const juce::String& key, const juce::Colour& value);

    juce::StringArray availableThemes() const;

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void themeChanged(Theme theme) = 0;
    };

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

private:
    ThemeEngine();
    ~ThemeEngine() = default;

    void pushTokens();
    void initDarkTheme();
    void initLightTheme();
    void initNordTheme();
    void initDraculaTheme();
    void initHighContrastTheme();

    Theme m_currentTheme = Dark;
    std::map<juce::String, juce::Colour> m_colors;
    juce::ListenerList<Listener> m_listeners;
};

} // namespace BeatMate::UI
