#pragma once
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include "SetPreparationView.h"
#include "PlaylistPreparationView.h"
#include "../IRetranslatable.h"

namespace BeatMate::Services::Library { class TrackDataProvider; }
namespace BeatMate::UI {

class PreparationHubView : public juce::Component,
                            public BeatMate::UI::IRetranslatable
{
public:
    explicit PreparationHubView(Services::Library::TrackDataProvider* provider);
    ~PreparationHubView() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void retranslateUi() override;

    void setMode(bool showLive);

    SetPreparationView* liveView() { return m_liveView.get(); }
    PlaylistPreparationView* playlistView() { return m_playlistView.get(); }

private:
    std::unique_ptr<juce::TextButton> m_liveModeBtn;
    std::unique_ptr<juce::TextButton> m_playlistModeBtn;
    std::unique_ptr<SetPreparationView> m_liveView;
    std::unique_ptr<PlaylistPreparationView> m_playlistView;
    bool m_showingLive = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreparationHubView)
};

} // namespace BeatMate::UI
