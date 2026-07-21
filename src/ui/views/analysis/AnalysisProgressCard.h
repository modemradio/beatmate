#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>
#include "../../IRetranslatable.h"
#include "../../widgets/controls/AnimatedProgressBar.h"

namespace BeatMate::UI {

class AnalysisProgressCard : public juce::Component, public IRetranslatable, private juce::Timer
{
public:
    enum class RunState { Idle, Running, Done, Cancelled };

    AnalysisProgressCard();
    ~AnalysisProgressCard() override;

    void beginRun();
    void setProgress(int processed, int total, int skipped);
    void trackStarted(const juce::String& path, const juce::String& title);
    void trackFinished(const juce::String& path);
    void setLastResult(const juce::String& bpm, const juce::String& key, const juce::String& energy);
    void endRun(bool cancelled);

    RunState getRunState() const { return m_state; }
    static constexpr int expandedHeight = 150;

    void retranslateUi() override;
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    RunState m_state = RunState::Idle;
    int m_processed = 0;
    int m_total = 0;
    int m_skipped = 0;
    double m_startTimeMs = 0.0;
    juce::String m_lastBpm, m_lastKey, m_lastEnergy;
    struct ActiveTrack { juce::String path, title; };
    std::vector<ActiveTrack> m_activeTracks;
    float m_pulsePhase = 0.0f;
    float m_spinnerAngle = 0.0f;
    std::unique_ptr<AnimatedProgressBar> m_bar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalysisProgressCard)
};

} // namespace BeatMate::UI
