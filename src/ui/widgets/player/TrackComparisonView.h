#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {

struct TrackComparisonData {
    juce::String title;
    juce::String artist;
    juce::String key;
    double bpm = 0.0;
    float energy = 0.0f;
    double durationSeconds = 0.0;
    juce::String genre;
    int year = 0;
    float loudnessLUFS = -14.0f;
};

class TrackComparisonView : public juce::Component {
public:
    TrackComparisonView();
    ~TrackComparisonView() override = default;

    void setTrackA(const TrackComparisonData& data);
    void setTrackB(const TrackComparisonData& data);
    void clearTracks();

    void paint(juce::Graphics& g) override;

private:
    void drawComparisonRow(juce::Graphics& g, int y, int h, const juce::String& label,
                           const juce::String& valA, const juce::String& valB,
                           float matchScore = -1.0f) const;
    float computeBPMMatch() const;
    float computeKeyMatch() const;
    float computeEnergyMatch() const;
    juce::Colour matchColor(float score) const;

    TrackComparisonData m_trackA, m_trackB;
    bool m_hasA = false, m_hasB = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackComparisonView)
};

} // namespace BeatMate::UI
