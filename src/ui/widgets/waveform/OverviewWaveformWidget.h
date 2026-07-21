#pragma once
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class OverviewWaveformWidget : public juce::Component {
public:
    OverviewWaveformWidget(); ~OverviewWaveformWidget() override=default;
    void setWaveformData(const std::vector<float>& peaks);
    void setVisibleRange(double start,double end);void setPlayheadPosition(double pos);
    void paint(juce::Graphics& g) override;void mouseDown(const juce::MouseEvent& e) override;
    class Listener{public:virtual ~Listener()=default;virtual void positionClicked(double){}};
    void addListener(Listener*l){m_listeners.add(l);}void removeListener(Listener*l){m_listeners.remove(l);}
private:
    std::vector<float> m_peaks;double m_visibleStart=0.0,m_visibleEnd=1.0,m_playheadPos=0.0;
    juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OverviewWaveformWidget)
};
} // namespace BeatMate::UI
