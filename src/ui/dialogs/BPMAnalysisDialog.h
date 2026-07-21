#pragma once
#include <memory>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class BPMAnalysisDialog : public juce::Component {
public:
    BPMAnalysisDialog();
    ~BPMAnalysisDialog() override = default;
    void setDetectedBPM(double bpm, double confidence);
    double bpm() const;
    void paint(juce::Graphics& g) override;
    void resized() override;
    std::function<void(double)> onBpmOverridden;
private:
    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::Label> m_detectedBPMLabel;
    std::unique_ptr<juce::Label> m_confidenceLabel;
    std::unique_ptr<juce::Label> m_overrideLabel;
    std::unique_ptr<juce::Slider> m_overrideSlider;
    std::unique_ptr<juce::TextButton> m_applyBtn;
    std::unique_ptr<juce::TextButton> m_cancelBtn;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BPMAnalysisDialog)
};
} // namespace BeatMate::UI
