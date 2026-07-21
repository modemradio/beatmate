#pragma once
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class EnergyGraphDialog : public juce::Component {
public:
    EnergyGraphDialog();
    ~EnergyGraphDialog() override = default;
    void setTrackData(const juce::String& title, const std::vector<float>& energyData);
    void paint(juce::Graphics& g) override;
    void resized() override;
private:
    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::TextButton> m_closeBtn;
    std::vector<float> m_energyData;
    juce::Rectangle<int> m_graphArea;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnergyGraphDialog)
};
} // namespace BeatMate::UI
