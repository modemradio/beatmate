#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class LEDButton : public juce::Component, public juce::Timer {
public:
    LEDButton(const juce::String& text=""); ~LEDButton() override=default;
    void setActive(bool active);bool isActive() const{return m_active;}
    void setLEDColor(const juce::Colour& c){m_ledColor=c;repaint();}void setText(const juce::String& t){m_text=t;repaint();}
    void setLEDSize(float size){m_ledSize=size;repaint();}
    void paint(juce::Graphics& g) override;void mouseDown(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void timerCallback() override;
    class Listener{public:virtual ~Listener()=default;virtual void clicked(){}virtual void toggled(bool){}};
    void addListener(Listener*l){m_listeners.add(l);}void removeListener(Listener*l){m_listeners.remove(l);}
private:
    bool m_active=false;
    bool m_hovered=false;
    juce::Colour m_ledColor{0xFF00FFA3};
    juce::String m_text;
    float m_ledSize=8.0f;
    float m_glowAlpha=0.0f;
    float m_targetGlowAlpha=0.0f;
    juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LEDButton)
};
} // namespace BeatMate::UI
