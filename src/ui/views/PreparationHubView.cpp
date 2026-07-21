#include "PreparationHubView.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"

namespace BeatMate::UI {

PreparationHubView::PreparationHubView(Services::Library::TrackDataProvider* provider)
{
    m_liveView = std::make_unique<SetPreparationView>(provider);
    m_playlistView = std::make_unique<PlaylistPreparationView>(provider);
    addChildComponent(*m_liveView);
    addChildComponent(*m_playlistView);

    m_liveModeBtn = std::make_unique<juce::TextButton>(BM_TJ("prepHub.mode.live"));
    m_liveModeBtn->setClickingTogglesState(true);
    m_liveModeBtn->setRadioGroupId(1001, juce::dontSendNotification);
    m_liveModeBtn->setColour(juce::TextButton::buttonOnColourId, Colors::primary());
    m_liveModeBtn->onClick = [this] { setMode(true); };
    addAndMakeVisible(*m_liveModeBtn);

    m_playlistModeBtn = std::make_unique<juce::TextButton>(BM_TJ("prepHub.mode.playlist"));
    m_playlistModeBtn->setClickingTogglesState(true);
    m_playlistModeBtn->setRadioGroupId(1001, juce::dontSendNotification);
    m_playlistModeBtn->setColour(juce::TextButton::buttonOnColourId, Colors::primary());
    m_playlistModeBtn->onClick = [this] { setMode(false); };
    addAndMakeVisible(*m_playlistModeBtn);

    setMode(true);
}

void PreparationHubView::setMode(bool showLive)
{
    m_showingLive = showLive;
    m_liveView->setVisible(showLive);
    m_playlistView->setVisible(!showLive);
    m_liveModeBtn->setToggleState(showLive, juce::dontSendNotification);
    m_playlistModeBtn->setToggleState(!showLive, juce::dontSendNotification);
    resized();
}

void PreparationHubView::paint(juce::Graphics& g)
{
    ProDraw::viewBackground(g, getWidth(), getHeight());
}

void PreparationHubView::resized()
{
    auto bounds = getLocalBounds();
    auto modeBar = bounds.removeFromTop(40);
    modeBar.reduce(8, 4);
    m_liveModeBtn->setBounds(modeBar.removeFromLeft(modeBar.getWidth() / 2).reduced(2, 0));
    m_playlistModeBtn->setBounds(modeBar.reduced(2, 0));

    if (m_showingLive)
        m_liveView->setBounds(bounds);
    else
        m_playlistView->setBounds(bounds);
}

void PreparationHubView::retranslateUi()
{
    m_liveModeBtn->setButtonText(BM_TJ("prepHub.mode.live"));
    m_playlistModeBtn->setButtonText(BM_TJ("prepHub.mode.playlist"));
    m_liveView->retranslateUi();
    m_playlistView->retranslateUi();
}

} // namespace BeatMate::UI
