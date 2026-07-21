#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class LoadingSpinner : public juce::Component, public juce::Timer {
public:
    LoadingSpinner(); ~LoadingSpinner() override=default;
    void start();void stop();void setColor(const juce::Colour& c){m_color=c;}void setOverlay(bool overlay){m_overlay=overlay;}
    void paint(juce::Graphics& g) override;void timerCallback() override;
private:
    int m_angle=0;juce::Colour m_color{0xFF0078D4};bool m_overlay=true,m_active=false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoadingSpinner)
};
} // namespace BeatMate::UI
