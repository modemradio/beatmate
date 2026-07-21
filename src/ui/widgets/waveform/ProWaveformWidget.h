#pragma once
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class ProWaveformWidget : public juce::Component, public juce::Timer {
public:
    ProWaveformWidget(); ~ProWaveformWidget() override;
    void setWaveformData(const std::vector<float>& peaks);
    void setPlayheadPosition(double pos);void setZoom(double zoom);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override;
    void timerCallback() override{repaint();}
    class Listener{public:virtual ~Listener()=default;virtual void positionClicked(double){}};
    void addListener(Listener*l){m_listeners.add(l);}void removeListener(Listener*l){m_listeners.remove(l);}
private:
    std::vector<float> m_peaks;double m_playheadPos=0.0,m_zoom=1.0,m_scrollOffset=0.0;
    juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProWaveformWidget)
};
} // namespace BeatMate::UI
