#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class VUMeterWidget : public juce::Component, public juce::Timer {
public:
    VUMeterWidget(); ~VUMeterWidget() override=default;
    void setLevel(float level); void setPeakHold(bool enabled);
    float level() const{return m_level;}
    void paint(juce::Graphics& g) override;
    void timerCallback() override;
private:
    float m_level=0.0f,m_peakLevel=0.0f,m_peakDecay=0.0f;bool m_peakHold=true,m_clipping=false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VUMeterWidget)
};
}
