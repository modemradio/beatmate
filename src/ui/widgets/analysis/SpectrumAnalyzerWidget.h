#pragma once
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class SpectrumAnalyzerWidget : public juce::Component, public juce::Timer {
public:
    SpectrumAnalyzerWidget(); ~SpectrumAnalyzerWidget() override=default;
    void setSpectrumData(const std::vector<float>& bands);void setBarCount(int count);
    void paint(juce::Graphics& g) override;void timerCallback() override;
private:
    std::vector<float> m_bands, m_displayBands, m_peakHold;
    int m_barCount=32;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzerWidget)
};
} // namespace BeatMate::UI
