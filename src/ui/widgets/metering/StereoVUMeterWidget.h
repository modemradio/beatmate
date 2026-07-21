#pragma once
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class VUMeterWidget;
class StereoVUMeterWidget : public juce::Component {
public:
    StereoVUMeterWidget(); ~StereoVUMeterWidget() override=default;
    void setLevels(float left,float right);
    void resized() override;
private:
    std::unique_ptr<VUMeterWidget> m_leftMeter,m_rightMeter;
    std::unique_ptr<juce::Label> m_lLabel,m_rLabel;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoVUMeterWidget)
};
} // namespace BeatMate::UI
