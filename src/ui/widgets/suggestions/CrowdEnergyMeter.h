#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

namespace BeatMate::Services::Library    { class TrackDatabase; }
namespace BeatMate::Services::History    { class SessionHistoryRecorder; }

namespace BeatMate::UI::Widgets {

class CrowdEnergyMeter : public juce::Component,
                         private juce::Timer
{
public:
    CrowdEnergyMeter();
    ~CrowdEnergyMeter() override;

    void setRecorder(std::shared_ptr<Services::History::SessionHistoryRecorder> recorder);
    void setDatabase(std::shared_ptr<Services::Library::TrackDatabase> db);

    void refresh();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Sample { int64_t playedAt = 0; float energy = 0.0f; };

    std::shared_ptr<Services::History::SessionHistoryRecorder> m_recorder;
    std::shared_ptr<Services::Library::TrackDatabase>           m_db;

    std::vector<Sample> m_samples;
    float               m_currentEnergy = 0.0f;

    std::unique_ptr<juce::Label> m_statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CrowdEnergyMeter)
};

} // namespace BeatMate::UI::Widgets
