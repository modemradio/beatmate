#pragma once
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class AudioAnalysisDialog : public juce::Component {
public:
    AudioAnalysisDialog();
    ~AudioAnalysisDialog() override = default;
    void setTrackInfo(const juce::String& title, const juce::String& artist,
                      double bpm, const juce::String& key, int energy, double lufs);
    void paint(juce::Graphics& g) override;
    void resized() override;
    static void showDialog(juce::Component* parent);
private:
    std::unique_ptr<juce::TabbedComponent> m_tabComponent;
    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::Label> m_bpmLabel, m_keyLabel, m_energyLabel, m_lufsLabel;
    std::unique_ptr<juce::TextButton> m_closeBtn;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioAnalysisDialog)
};
} // namespace BeatMate::UI
