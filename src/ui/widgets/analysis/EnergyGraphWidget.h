#pragma once
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class EnergyGraphWidget : public juce::Component {
public:
    EnergyGraphWidget(); ~EnergyGraphWidget() override=default;
    void setEnergyData(const std::vector<float>& data);void setScale(int min,int max);
    void paint(juce::Graphics& g) override;
private:
    std::vector<float> m_data;int m_scaleMin=1,m_scaleMax=10;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnergyGraphWidget)
};
} // namespace BeatMate::UI
