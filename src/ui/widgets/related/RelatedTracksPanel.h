#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../../models/Track.h"

namespace BeatMate::Services::Library { class TrackDataProvider; }

namespace BeatMate::UI::Widgets {

// Rekordbox-style Related Tracks panel: 5 tabs filtering the library by
class RelatedTracksPanel : public juce::Component
{
public:
    enum class Relation {
        SameBPM    = 0,
        Compatible = 1,
        SameGenre  = 2,
        SameArtist = 3,
        SameEra    = 4
    };

    explicit RelatedTracksPanel(Services::Library::TrackDataProvider* provider);
    ~RelatedTracksPanel() override;

    void setReferenceTrack(const Models::Track& track);
    void clear();

    std::function<void(const Models::Track&)> onTrackChosen;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void refreshFor(Relation r);
    std::vector<Models::Track> filterByRelation(Relation r) const;

    class ResultsListModel;

    Services::Library::TrackDataProvider* m_provider = nullptr;
    Models::Track m_reference;
    bool m_hasReference = false;

    std::unique_ptr<juce::Label>    m_titleLabel;
    std::unique_ptr<juce::TabbedButtonBar> m_tabs;
    std::unique_ptr<juce::ListBox>  m_listBox;
    std::unique_ptr<ResultsListModel> m_listModel;

    Relation m_currentTab = Relation::SameBPM;
    std::vector<Models::Track> m_results;
};

} // namespace BeatMate::UI::Widgets
