#pragma once
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class MatchupDialog : public juce::Component {
public:
    MatchupDialog();
    ~MatchupDialog() override = default;
    void setTracks(const juce::String& title1, const juce::String& artist1, double bpm1, const juce::String& key1, int energy1,
                   const juce::String& title2, const juce::String& artist2, double bpm2, const juce::String& key2, int energy2);
    void paint(juce::Graphics& g) override;
    void resized() override;
private:
    void calculateCompatibility();
    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::Label> m_track1Label, m_track2Label;
    std::unique_ptr<juce::Label> m_scoreLabel;
    std::unique_ptr<juce::Label> m_bpmDiffLabel, m_keyCompatLabel, m_energyDiffLabel;
    std::unique_ptr<juce::TextButton> m_closeBtn;
    double m_bpm1=0, m_bpm2=0;
    juce::String m_key1, m_key2;
    int m_energy1=0, m_energy2=0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MatchupDialog)
};
} // namespace BeatMate::UI
