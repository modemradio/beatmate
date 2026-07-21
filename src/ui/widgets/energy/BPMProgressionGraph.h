#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../../models/Track.h"

namespace BeatMate::UI::Widgets {

// Line plot of BPM across setlist positions. Segments whose ΔBPM exceeds
class BPMProgressionGraph : public juce::Component
{
public:
    BPMProgressionGraph();
    ~BPMProgressionGraph() override = default;

    void setTracks(const std::vector<Models::Track>& tracks);
    void setJumpThreshold(double bpmDelta) { m_jumpThreshold = bpmDelta; repaint(); }

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;

private:
    std::vector<Models::Track> m_tracks;
    double m_jumpThreshold = 4.0;
    int m_hoverIndex = -1;
};

} // namespace BeatMate::UI::Widgets
