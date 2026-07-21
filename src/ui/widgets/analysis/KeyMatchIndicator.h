#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {

// KeyMatchIndicator - Key compatibility indicator (green/yellow/red)

class KeyMatchIndicator : public juce::Component {
public:
    enum MatchLevel { Perfect = 0, Compatible, Neutral, Clash, Unknown };

    KeyMatchIndicator();
    ~KeyMatchIndicator() override = default;

    void setKeys(const juce::String& key1, const juce::String& key2);
    void setMatchLevel(MatchLevel level);
    MatchLevel getMatchLevel() const { return m_matchLevel; }

    void setShowLabels(bool show) { m_showLabels = show; repaint(); }
    void setCompact(bool compact) { m_compact = compact; repaint(); }

    void paint(juce::Graphics& g) override;

    static MatchLevel computeMatch(const juce::String& key1, const juce::String& key2);

private:
    juce::Colour getMatchColor() const;
    juce::String getMatchText() const;

    juce::String m_key1, m_key2;
    MatchLevel m_matchLevel = Unknown;
    bool m_showLabels = true;
    bool m_compact = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeyMatchIndicator)
};

} // namespace BeatMate::UI
