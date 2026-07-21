#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>
namespace BeatMate::UI {

class StatusLEDPanel : public juce::Component, public juce::Timer {
public:
    enum LEDId { KEY = 0, SYNC, QUANT, LOOP, REC, SLIP, MASTER, LED_COUNT };

    StatusLEDPanel();
    ~StatusLEDPanel() override = default;

    void setLED(LEDId led, bool active);
    void setLEDFlashing(LEDId led, bool flashing);
    bool isLEDActive(LEDId led) const;

    void setOrientation(bool horizontal) { m_horizontal = horizontal; resized(); repaint(); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent& e) override;

    class Listener { public: virtual ~Listener() = default; virtual void ledClicked(LEDId led) {} };
    void addListener(Listener* l) { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

private:
    struct LEDState {
        bool active = false;
        bool flashing = false;
        juce::Colour color;
        juce::String label;
        juce::Rectangle<int> bounds;
    };
    std::vector<LEDState> m_leds;
    bool m_horizontal = true;
    bool m_flashPhase = false;
    juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusLEDPanel)
};

} // namespace BeatMate::UI
