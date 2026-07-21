#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class ModernToggleSwitch : public juce::Component, public juce::Timer {
public:
    ModernToggleSwitch(); ~ModernToggleSwitch() override=default;
    bool isOn() const{return m_on;} void setOn(bool on);
    void paint(juce::Graphics& g) override;void mouseDown(const juce::MouseEvent&) override;
    void timerCallback() override;
    class Listener{public:virtual ~Listener()=default;virtual void toggled(bool){}};
    void addListener(Listener*l){m_listeners.add(l);}void removeListener(Listener*l){m_listeners.remove(l);}
private:
    bool m_on=false;double m_position=0.0,m_targetPosition=0.0;
    juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModernToggleSwitch)
};
}
