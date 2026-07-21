#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../../models/Track.h"

namespace BeatMate::UI::Widgets {

// Djify-style editable target energy curve (0..10) over the set duration.
class EnergyCurveEditor : public juce::Component
{
public:
    EnergyCurveEditor();
    ~EnergyCurveEditor() override = default;

    void setTracks(const std::vector<Models::Track>& tracks);
    void setControlPoints(const std::vector<float>& y0to10);  // expects size() == 4
    std::vector<float> getControlPoints() const { return m_cp; }

    // Sample target energy (0..10) at normalized x in [0,1]
    float sampleCurveAt(float normalizedX) const;

    std::function<void()> onCurveChanged;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    juce::Rectangle<float> plotBounds() const;
    juce::Point<float> cpScreen(int i) const;
    int hitTestControlPoint(juce::Point<float> p) const;

    std::vector<float> m_cp { 3.0f, 7.0f, 8.5f, 4.0f };  // 4 control points 0..10
    std::vector<Models::Track> m_tracks;
    int m_dragIndex = -1;
};

} // namespace BeatMate::UI::Widgets
