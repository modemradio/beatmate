#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>
namespace BeatMate::UI {

struct PhraseSegment {
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    float energy = 0.0f;       // 0..1
    juce::String label;         // "Intro", "Verse", "Chorus", "Drop", "Break", "Outro"
    juce::Colour color;
    int phraseNumber = 0;
};

class PhraseVisualizer : public juce::Component {
public:
    PhraseVisualizer();
    ~PhraseVisualizer() override = default;

    void setPhrases(const std::vector<PhraseSegment>& phrases);
    void setTotalDuration(double seconds) { m_totalDuration = seconds; repaint(); }
    void setCurrentPosition(double seconds) { m_currentPosition = seconds; repaint(); }
    void setShowLabels(bool show) { m_showLabels = show; repaint(); }
    void setShowEnergy(bool show) { m_showEnergy = show; repaint(); }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

    class Listener { public: virtual ~Listener() = default; virtual void phraseClicked(int phraseIndex, double positionSeconds) {} };
    void addListener(Listener* l) { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

private:
    juce::Colour getDefaultColor(const juce::String& label) const;

    std::vector<PhraseSegment> m_phrases;
    double m_totalDuration = 0.0;
    double m_currentPosition = 0.0;
    bool m_showLabels = true;
    bool m_showEnergy = true;
    juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhraseVisualizer)
};

} // namespace BeatMate::UI
