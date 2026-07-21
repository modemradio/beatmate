#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../../models/Track.h"
#include "../../../services/preparation/CamelotMoveClassifier.h"

namespace BeatMate::UI::Widgets {

class HarmonicCompatibilityChip : public juce::Component
{
public:
    HarmonicCompatibilityChip();
    ~HarmonicCompatibilityChip() override = default;

    void setTracks(const Models::Track& from, const Models::Track& to);
    void setScore(int score0to100);
    void clear();

    void paint(juce::Graphics& g) override;

private:
    int m_score = 0;
    bool m_hasData = false;
    juce::String m_moveLabel;
    juce::Colour m_moveColor;
    double m_bpmDelta = 0.0;
    int m_energyDelta = 0;

    Services::Preparation::CamelotMoveClassifier m_classifier;
};

} // namespace BeatMate::UI::Widgets
