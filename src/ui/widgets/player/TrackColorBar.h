#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {

class TrackColorBar : public juce::Component {
public:
    TrackColorBar();
    ~TrackColorBar() override = default;

    void setColor(const juce::Colour& color);
    juce::Colour getColor() const { return m_color; }

    void setTrackTitle(const juce::String& title) { m_title = title; repaint(); }
    void setShowTitle(bool show) { m_showTitle = show; repaint(); }
    void setOrientation(bool horizontal) { m_horizontal = horizontal; repaint(); }
    void setBarThickness(int thickness) { m_thickness = thickness; repaint(); }
    void setGlowEnabled(bool enabled) { m_glow = enabled; repaint(); }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

    class Listener { public: virtual ~Listener() = default; virtual void colorChanged(const juce::Colour& c) {} };
    void addListener(Listener* l) { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

    static std::vector<juce::Colour> getPresetColors();

private:
    juce::Colour m_color{0xFF0078D4};
    juce::String m_title;
    bool m_showTitle = false;
    bool m_horizontal = true;
    bool m_glow = true;
    int m_thickness = 4;
    juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackColorBar)
};

} // namespace BeatMate::UI
