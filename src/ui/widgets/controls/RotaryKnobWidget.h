#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class RotaryKnobWidget : public juce::Component {
public:
    RotaryKnobWidget(); ~RotaryKnobWidget() override=default;
    double value() const{return m_value;} void setValue(double v);
    void setRange(double min,double max); void setDefaultValue(double v){m_defaultValue=v;}
    void setLabel(const juce::String& label){m_label=label;repaint();}
    void setColor(const juce::Colour& c){m_color=c;repaint();}
    void setSuffix(const juce::String& s){m_suffix=s;repaint();}
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails& w) override;
    class Listener{public:virtual ~Listener()=default;virtual void valueChanged(double){}};
    void addListener(Listener*l){m_listeners.add(l);}void removeListener(Listener*l){m_listeners.remove(l);}
private:
    double m_value=0.0,m_min=0.0,m_max=100.0,m_defaultValue=0.0;
    juce::String m_label,m_suffix;juce::Colour m_color{0xFF00D9FF};juce::Point<int> m_lastMousePos;bool m_dragging=false;
    juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RotaryKnobWidget)
};
} // namespace BeatMate::UI
