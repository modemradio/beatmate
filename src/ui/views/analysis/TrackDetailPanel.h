#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>
#include "../../IRetranslatable.h"
#include "../../../models/Track.h"
#include "../../widgets/analysis/EnergyArcWidget.h"
#include "../../widgets/analysis/EnergyGraphWidget.h"
#include "../../widgets/analysis/CamelotWheelWidget.h"
#include "../../widgets/analysis/TrackStructureVisualizer.h"
#include "../../widgets/waveform/MiniWaveformInline.h"

namespace BeatMate::UI {

class TrackDetailPanel : public juce::Component, public IRetranslatable
{
public:
    TrackDetailPanel();
    ~TrackDetailPanel() override;

    void setTrack(const Models::Track& track);
    void clearTrack();
    bool hasTrack() const { return m_hasTrack; }
    int64_t currentTrackId() const { return m_track.id; }

    void retranslateUi() override;
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Content : public juce::Component
    {
    public:
        explicit Content(TrackDetailPanel& owner);
        void paint(juce::Graphics& g) override;
        void resized() override;
        TrackDetailPanel& panel;
    };

    void rebuildFromTrack();
    void loadWaveformAsync();
    juce::String confidenceText(float confidence) const;

    Models::Track m_track;
    bool m_hasTrack = false;
    bool m_hasWaveform = false;
    bool m_hasEnergyCurve = false;
    bool m_hasStructure = false;
    int m_waveformGeneration = 0;

    juce::Viewport m_viewport;
    std::unique_ptr<Content> m_content;
    std::unique_ptr<EnergyArcWidget> m_energyArc;
    std::unique_ptr<EnergyGraphWidget> m_energyGraph;
    std::unique_ptr<CamelotWheelWidget> m_camelotWheel;
    std::unique_ptr<TrackStructureVisualizer> m_structure;
    std::unique_ptr<MiniWaveformInline> m_waveform;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackDetailPanel)
};

} // namespace BeatMate::UI
