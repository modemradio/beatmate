#pragma once
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class PerformanceMonitorWidget : public juce::Component, public juce::Timer {
public:
    PerformanceMonitorWidget(); ~PerformanceMonitorWidget() override=default;
    void setCPU(double percent);void setRAM(int mb);void setLatency(double ms);
    void resized() override;void timerCallback() override;
private:
    void updateDisplay();
    std::unique_ptr<juce::Label> m_cpuLabel,m_ramLabel,m_latencyLabel;
    double m_cpu=0;int m_ram=0;double m_latency=0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PerformanceMonitorWidget)
};
} // namespace BeatMate::UI
