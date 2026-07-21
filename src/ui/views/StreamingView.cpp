#include "StreamingView.h"
#include "../styles/ColorPalette.h"
#include "../../services/library/TrackDataProvider.h"
#include "../../services/streaming/SpotifyService.h"
#include "../../services/config/I18n.h"
#include "../../services/streaming/ChartsService.h"
#include "../widgets/suggestions/LivePreviewPlayer.h"
#include "../widgets/suggestions/YouTubePreview.h"
#include "../widgets/player/NowPlayingBar.h"
#include "../widgets/ToastNotifier.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <spdlog/spdlog.h>
#include <ctime>

namespace BeatMate::UI {
namespace {
static bool trackHasLocalFile(const Models::Track& t)
{
    if (t.filePath.empty()) return false;
    const juce::String p(t.filePath);
    return juce::File::isAbsolutePath(p) && juce::File(p).existsAsFile();
}

static void playBestSource(const Models::Track& t, const std::string& previewUrl)
{
    if (trackHasLocalFile(t))
    {
        if (auto* bar = NowPlayingBar::instance())
        {
            Widgets::LivePreviewPlayer::getInstance().stop();
            const juce::String key = juce::String::fromUTF8(
                (t.camelotKey.empty() ? t.key : t.camelotKey).c_str());
            bar->loadAndPlay(juce::String::fromUTF8(t.filePath.c_str()),
                             juce::String::fromUTF8(t.title.c_str()),
                             juce::String::fromUTF8(t.artist.c_str()),
                             t.bpm, key.isEmpty() ? juce::String("-") : key);
            return;
        }
        Widgets::LivePreviewPlayer::getInstance().playPreview(
            t.artist + "|" + t.title, t.artist, t.title, t.filePath);
        return;
    }
    Widgets::LivePreviewPlayer::getInstance().playPreview(
        t.artist + "|" + t.title, t.artist, t.title, previewUrl);
}

static void openYouTubePreview(const Models::Track& t)
{
    Widgets::YouTubePreview::showForTrack(juce::String::fromUTF8(t.artist.c_str()),
                                          juce::String::fromUTF8(t.title.c_str()));
}

static void stopAllPreviews()
{
    Widgets::LivePreviewPlayer::getInstance().stop();
    Widgets::YouTubePreview::stopPlayback();
    if (auto* bar = NowPlayingBar::instance())
        bar->stopPlayback();
}

static void wireStreamingTrackActions(TrackTablePanel* panel,
                                      Services::Library::TrackDataProvider* provider)
{
    if (!panel) return;
    panel->onTrackDoubleClicked = [](const Models::Track& t) {
        playBestSource(t, {});
    };
    panel->onYoutubeClicked = [](int, const Models::Track& t) {
        openYouTubePreview(t);
    };
    panel->onTrackRightClicked = [provider](const Models::Track& t, juce::Point<int> screenPos) {
        juce::PopupMenu m;
        m.addItem(1, trackHasLocalFile(t)
                         ? juce::String::fromUTF8("\xE2\x96\xB6 Ecouter (bibliotheque, complet)")
                         : juce::String::fromUTF8("\xE2\x96\xB6 Ecouter l'extrait (30s)"));
        m.addItem(4, juce::String::fromUTF8("Preecoute YouTube"));
        m.addItem(2, juce::String::fromUTF8("\xE2\x8F\xB9 Arreter la preecoute"));
        m.addSeparator();
        m.addItem(3, juce::String::fromUTF8("Ajouter a ma bibliotheque"), provider != nullptr);
        const Models::Track track = t;
        m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
            juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
            [track, provider](int r) {
                if (r == 1)
                    playBestSource(track, {});
                else if (r == 4)
                    openYouTubePreview(track);
                else if (r == 2)
                    stopAllPreviews();
                else if (r == 3 && provider) {
                    if (track.filePath.empty()) {
                        Widgets::ToastNotifier::getInstance().show(
                            juce::String::fromUTF8("Piste streaming"),
                            juce::String::fromUTF8("Pas de fichier local : recherchez-la sur vos plateformes ou importez-la."),
                            Widgets::ToastNotifier::Kind::Warning);
                        return;
                    }
                    provider->addTrack(track);
                    Widgets::ToastNotifier::getInstance().show(
                        juce::String::fromUTF8("Ajoutee a la bibliotheque"),
                        juce::String(track.artist) + " - " + juce::String(track.title),
                        Widgets::ToastNotifier::Kind::Success);
                }
            });
    };
}
} // namespace
} // namespace BeatMate::UI

namespace BeatMate::UI {

TrackTablePanel::TrackTablePanel(const juce::String& sectionTitle, bool showRank)
    : m_showRank(showRank)
{
    m_sectionLabel.setText(sectionTitle, juce::dontSendNotification);
    m_sectionLabel.setFont(juce::Font(15.0f, juce::Font::bold));
    m_sectionLabel.setColour(juce::Label::textColourId, Colors::textPrimary());
    addAndMakeVisible(m_sectionLabel);

    m_table = std::make_unique<juce::TableListBox>("trackTable", this);
    m_table->setColour(juce::ListBox::backgroundColourId, Colors::bgSurface());
    m_table->setColour(juce::ListBox::outlineColourId, Colors::border());
    m_table->setRowHeight(28);
    m_table->setHeaderHeight(26);
    m_table->getHeader().setColour(juce::TableHeaderComponent::backgroundColourId, Colors::bgCard());
    m_table->getHeader().setColour(juce::TableHeaderComponent::textColourId, Colors::textSecondary());
    addAndMakeVisible(*m_table);
}

void TrackTablePanel::addColumns(std::initializer_list<ColumnId> cols, bool sortable)
{
    int flags = sortable ? (juce::TableHeaderComponent::defaultFlags | juce::TableHeaderComponent::sortable)
                         : juce::TableHeaderComponent::defaultFlags;
    for (auto c : cols) {
        switch (c) {
            case ColRank:   m_table->getHeader().addColumn("#",       ColRank,   36, 30, 50, flags); break;
            case ColTitle:  m_table->getHeader().addColumn(BM_TJ("analysis.col.title"),   ColTitle,  220, 100, 600, flags); break;
            case ColArtist: m_table->getHeader().addColumn(BM_TJ("analysis.col.artist"), ColArtist, 160, 80, 400, flags); break;
            case ColBPM:    m_table->getHeader().addColumn("BPM",     ColBPM,    60, 45, 80, flags); break;
            case ColKey:    m_table->getHeader().addColumn("Key",     ColKey,    55, 40, 70, flags); break;
            case ColGenre:  m_table->getHeader().addColumn("Genre",   ColGenre,  100, 60, 200, flags); break;
            case ColPlays:  m_table->getHeader().addColumn("Plays",   ColPlays,  55, 40, 80, flags); break;
            case ColEnergy: m_table->getHeader().addColumn("Energy",  ColEnergy, 55, 40, 80, flags); break;
            case ColDate:   m_table->getHeader().addColumn("Date",    ColDate,   110, 70, 160, flags); break;
            case ColCues:   m_table->getHeader().addColumn("Cues",    ColCues,   45, 35, 60, flags); break;
            case ColDelta:  m_table->getHeader().addColumn("Tendance", ColDelta, 70, 50, 90, flags); break;
            case ColLib:    m_table->getHeader().addColumn("Biblio",  ColLib,    62, 45, 80, flags); break;
            case ColYt:     m_table->getHeader().addColumn("YouTube", ColYt,     66, 50, 90, juce::TableHeaderComponent::visible); break;
        }
    }
}

void TrackTablePanel::setEmptyMessageKey(const juce::String& key)
{
    m_emptyMessageKey = key;
    m_emptyMessage = BM_TJ(key.toStdString());
}

void TrackTablePanel::retranslateUi()
{
    if (m_table) {
        m_table->getHeader().setColumnName(ColTitle,  BM_TJ("analysis.col.title"));
        m_table->getHeader().setColumnName(ColArtist, BM_TJ("analysis.col.artist"));
    }
    if (m_emptyMessageKey.isNotEmpty())
        m_emptyMessage = BM_TJ(m_emptyMessageKey.toStdString());
    repaint();
}

void TrackTablePanel::setTracks(const std::vector<Models::Track>& tracks)
{
    m_tracks = tracks;
    m_chartMeta.clear();
    m_table->updateContent();
    m_table->repaint();
}

void TrackTablePanel::setChartMeta(std::vector<ChartRowMeta> meta)
{
    m_chartMeta = std::move(meta);
    m_table->repaint();
}

const TrackTablePanel::ChartRowMeta* TrackTablePanel::chartMetaFor(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_chartMeta.size())) return nullptr;
    return &m_chartMeta[static_cast<size_t>(row)];
}

int TrackTablePanel::getNumRows() { return static_cast<int>(m_tracks.size()); }

void TrackTablePanel::paintRowBackground(juce::Graphics& g, int row, int, int, bool selected)
{
    if (selected)
        g.fillAll(Colors::primary().withAlpha(0.25f));
    else if (row % 2 == 1)
        g.fillAll(Colors::bgCard().withAlpha(0.4f));
    else
        g.fillAll(Colors::bgSurface());
}

void TrackTablePanel::drawBadge(juce::Graphics& g, const juce::String& text,
                                 juce::Colour bgCol, int x, int y, int w, int h) const
{
    auto badgeW = juce::jmin(w - 6, static_cast<int>(juce::Font(11.0f).getStringWidthFloat(text)) + 12);
    auto badgeX = x + (w - badgeW) / 2;
    auto badgeY = y + 4;
    auto badgeH = h - 8;
    g.setColour(bgCol.withAlpha(0.2f));
    g.fillRoundedRectangle(static_cast<float>(badgeX), static_cast<float>(badgeY),
                           static_cast<float>(badgeW), static_cast<float>(badgeH), 4.0f);
    g.setColour(bgCol);
    g.drawRoundedRectangle(static_cast<float>(badgeX), static_cast<float>(badgeY),
                           static_cast<float>(badgeW), static_cast<float>(badgeH), 4.0f, 0.8f);
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.drawText(text, badgeX, badgeY, badgeW, badgeH, juce::Justification::centred);
}

juce::String TrackTablePanel::formatRelativeDate(int64_t timestamp) const
{
    if (timestamp <= 0) return "-";
    auto now = static_cast<int64_t>(std::time(nullptr));
    auto diff = now - timestamp;
    if (diff < 60)        return "A l'instant";
    if (diff < 3600)      return juce::String(diff / 60) + " min";
    if (diff < 86400)     return juce::String(diff / 3600) + " h";
    if (diff < 604800)    return juce::String(diff / 86400) + " j";
    if (diff < 2592000)   return juce::String(diff / 604800) + " sem";
    if (diff < 31536000)  return juce::String(diff / 2592000) + " mois";
    return juce::String(diff / 31536000) + " an(s)";
}

void TrackTablePanel::paintCell(juce::Graphics& g, int row, int colId, int w, int h, bool)
{
    if (row < 0 || row >= static_cast<int>(m_tracks.size())) return;
    const auto& t = m_tracks[static_cast<size_t>(row)];

    g.setFont(juce::Font(12.0f));
    auto textArea = juce::Rectangle<int>(6, 0, w - 12, h);

    switch (colId) {
        case ColRank:
            g.setColour(Colors::textSecondary());
            g.setFont(juce::Font(11.0f, juce::Font::bold));
            g.drawText(juce::String(row + 1), textArea, juce::Justification::centred);
            break;

        case ColTitle:
            g.setColour(Colors::textPrimary());
            g.setFont(juce::Font(12.0f, juce::Font::bold));
            g.drawText(juce::String(t.title), textArea, juce::Justification::centredLeft, true);
            break;

        case ColArtist:
            g.setColour(Colors::textSecondary());
            g.drawText(juce::String(t.artist), textArea, juce::Justification::centredLeft, true);
            break;

        case ColBPM:
            if (t.bpm > 0.0) {
                drawBadge(g, juce::String(t.bpm, 1), Colors::bpmBadge(), 0, 0, w, h);
            } else {
                g.setColour(Colors::textMuted());
                g.drawText("-", textArea, juce::Justification::centred);
            }
            break;

        case ColKey: {
            auto keyStr = t.camelotKey.empty() ? t.key : t.camelotKey;
            if (!keyStr.empty()) {
                drawBadge(g, juce::String(keyStr), Colors::keyBadge(), 0, 0, w, h);
            } else {
                g.setColour(Colors::textMuted());
                g.drawText("-", textArea, juce::Justification::centred);
            }
            break;
        }

        case ColGenre:
            g.setColour(Colors::textSecondary());
            g.drawText(juce::String(t.genre), textArea, juce::Justification::centredLeft, true);
            break;

        case ColPlays:
            g.setColour(t.playCount > 0 ? Colors::primary() : Colors::textMuted());
            g.setFont(juce::Font(11.0f, juce::Font::bold));
            g.drawText(juce::String(t.playCount), textArea, juce::Justification::centred);
            break;

        case ColEnergy:
            if (t.energy > 0.0f) {
                auto energyCol = t.energy > 7.0f ? Colors::energyHigh()
                               : t.energy > 4.0f ? Colors::energyMedium()
                               : Colors::energyLow();
                drawBadge(g, juce::String(static_cast<int>(t.energy)), energyCol, 0, 0, w, h);
            } else {
                g.setColour(Colors::textMuted());
                g.drawText("-", textArea, juce::Justification::centred);
            }
            break;

        case ColDate:
            g.setColour(Colors::textSecondary());
            g.setFont(juce::Font(11.0f));
            g.drawText(formatRelativeDate(t.dateAdded), textArea, juce::Justification::centredLeft);
            break;

        case ColCues:
            g.setColour(t.analyzed ? Colors::success() : Colors::textMuted());
            g.setFont(juce::Font(14.0f));
            g.drawText(t.analyzed ? juce::CharPointer_UTF8("\xe2\x97\x8f") : juce::CharPointer_UTF8("\xe2\x97\x8b"),
                       textArea, juce::Justification::centred);
            break;

        case ColDelta: {
            const auto* m = chartMetaFor(row);
            if (m && m->hasDelta) {
                if (m->isNew)
                    drawBadge(g, "NEW", Colors::accent(), 0, 0, w, h);
                else if (m->delta > 0)
                    drawBadge(g, juce::String::fromUTF8("\xe2\x86\x91") + juce::String(m->delta),
                              Colors::success(), 0, 0, w, h);
                else if (m->delta < 0)
                    drawBadge(g, juce::String::fromUTF8("\xe2\x86\x93") + juce::String(-m->delta),
                              Colors::error(), 0, 0, w, h);
                else
                    drawBadge(g, "=", Colors::textSecondary(), 0, 0, w, h);
            } else {
                g.setColour(Colors::textMuted());
                g.drawText("-", textArea, juce::Justification::centred);
            }
            break;
        }

        case ColLib: {
            const auto* m = chartMetaFor(row);
            if (m && m->inLibrary) {
                drawBadge(g, "BIBLIO", Colors::accent(), 0, 0, w, h);
            } else {
                g.setColour(Colors::textMuted());
                g.drawText("-", textArea, juce::Justification::centred);
            }
            break;
        }

        case ColYt:
            drawBadge(g, "YT", juce::Colour(0xFFFF4444), 0, 0, w, h);
            break;
    }
}

void TrackTablePanel::sortOrderChanged(int newSortColumnId, bool isForwards)
{
    if (!m_chartMeta.empty()) return;
    auto compare = [&](const Models::Track& a, const Models::Track& b) -> bool {
        int result = 0;
        switch (newSortColumnId) {
            case ColTitle:  result = juce::String(a.title).compareIgnoreCase(juce::String(b.title)); break;
            case ColArtist: result = juce::String(a.artist).compareIgnoreCase(juce::String(b.artist)); break;
            case ColBPM:    result = (a.bpm < b.bpm) ? -1 : (a.bpm > b.bpm ? 1 : 0); break;
            case ColKey: {
                auto ka = a.camelotKey.empty() ? a.key : a.camelotKey;
                auto kb = b.camelotKey.empty() ? b.key : b.camelotKey;
                result = juce::String(ka).compareIgnoreCase(juce::String(kb));
                break;
            }
            case ColGenre:  result = juce::String(a.genre).compareIgnoreCase(juce::String(b.genre)); break;
            case ColPlays:  result = a.playCount - b.playCount; break;
            case ColEnergy: result = (a.energy < b.energy) ? -1 : (a.energy > b.energy ? 1 : 0); break;
            case ColDate:   result = (a.dateAdded < b.dateAdded) ? -1 : (a.dateAdded > b.dateAdded ? 1 : 0); break;
            default: break;
        }
        return isForwards ? (result < 0) : (result > 0);
    };
    std::sort(m_tracks.begin(), m_tracks.end(), compare);
    m_table->updateContent();
    m_table->repaint();
}

void TrackTablePanel::cellDoubleClicked(int rowNumber, int, const juce::MouseEvent&)
{
    if (rowNumber < 0 || rowNumber >= static_cast<int>(m_tracks.size())) return;
    if (onRowDoubleClicked)
        onRowDoubleClicked(rowNumber, m_tracks[static_cast<size_t>(rowNumber)]);
    else if (onTrackDoubleClicked)
        onTrackDoubleClicked(m_tracks[static_cast<size_t>(rowNumber)]);
}

void TrackTablePanel::cellClicked(int rowNumber, int columnId, const juce::MouseEvent& e)
{
    if (rowNumber < 0 || rowNumber >= static_cast<int>(m_tracks.size()))
        return;
    if (e.mods.isPopupMenu()) {
        if (onRowRightClicked)
            onRowRightClicked(rowNumber, m_tracks[static_cast<size_t>(rowNumber)], e.getScreenPosition());
        else if (onTrackRightClicked)
            onTrackRightClicked(m_tracks[static_cast<size_t>(rowNumber)], e.getScreenPosition());
        return;
    }
    if (columnId == ColYt && onYoutubeClicked)
        onYoutubeClicked(rowNumber, m_tracks[static_cast<size_t>(rowNumber)]);
}

void TrackTablePanel::paint(juce::Graphics& g)
{
    g.setColour(Colors::bgCard());
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
    g.setColour(Colors::glassBorder());
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 8.0f, 1.0f);

    if (m_tracks.empty()) {
        auto area = getLocalBounds().reduced(12, 8);
        area.removeFromTop(28);
        g.setColour(Colors::textMuted());
        g.setFont(juce::Font(13.0f));
        g.drawText(m_emptyMessage, area, juce::Justification::centred, true);
    }
}

void TrackTablePanel::resized()
{
    auto area = getLocalBounds().reduced(12, 8);
    m_sectionLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(4);
    m_table->setBounds(area);
}

HistoriqueTab::HistoriqueTab(Services::Library::TrackDataProvider* provider)
    : m_provider(provider)
{
    m_history = std::make_unique<TrackTablePanel>("HISTORIQUE DE LECTURE", true);
    m_history->addColumns({TrackTablePanel::ColRank, TrackTablePanel::ColTitle,
                           TrackTablePanel::ColArtist, TrackTablePanel::ColBPM,
                           TrackTablePanel::ColKey, TrackTablePanel::ColPlays,
                           TrackTablePanel::ColYt});
    m_history->setEmptyMessage("Aucune lecture enregistree. Lancez des morceaux depuis la bibliotheque !");
    addAndMakeVisible(*m_history);
    wireStreamingTrackActions(m_history.get(), m_provider);
}

void HistoriqueTab::visibilityChanged()
{
    if (isVisible())
        refresh();
}

void HistoriqueTab::refresh()
{
    if (!m_provider || !m_history)
        return;
    auto all = m_provider->getAllTracks();
    std::vector<Models::Track> played;
    for (auto& t : all)
        if (t.lastPlayed > 0)
            played.push_back(std::move(t));
    std::sort(played.begin(), played.end(),
              [](const Models::Track& a, const Models::Track& b) {
                  return a.lastPlayed > b.lastPlayed;
              });
    if (played.size() > 100)
        played.resize(100);
    m_history->setTracks(played);
}

void HistoriqueTab::paint(juce::Graphics& g)
{
    ProDraw::viewBackground(g, getWidth(), getHeight());
}

void HistoriqueTab::resized()
{
    if (m_history)
        m_history->setBounds(getLocalBounds().reduced(16));
}

void HistoriqueTab::retranslateUi()
{
    repaint();
}

TendancesTab::TendancesTab(Services::Library::TrackDataProvider* provider)
    : m_provider(provider)
{
    m_viewport = std::make_unique<juce::Viewport>();
    m_content = std::make_unique<juce::Component>();
    m_viewport->setViewedComponent(m_content.get(), false);
    m_viewport->setScrollBarsShown(true, false);
    addAndMakeVisible(*m_viewport);

    m_topPlayed = std::make_unique<TrackTablePanel>("TOP 50 DE VOTRE COLLECTION", true);
    m_topPlayed->addColumns({TrackTablePanel::ColRank, TrackTablePanel::ColTitle,
                             TrackTablePanel::ColArtist, TrackTablePanel::ColYt,
                             TrackTablePanel::ColBPM, TrackTablePanel::ColKey,
                             TrackTablePanel::ColPlays, TrackTablePanel::ColEnergy});
    m_topPlayed->setEmptyMessage("Aucune piste dans votre collection. Importez de la musique pour commencer !");
    m_content->addAndMakeVisible(*m_topPlayed);

    m_recentlyAdded = std::make_unique<TrackTablePanel>("RECEMMENT AJOUTEES", false);
    m_recentlyAdded->addColumns({TrackTablePanel::ColTitle, TrackTablePanel::ColArtist,
                                  TrackTablePanel::ColYt, TrackTablePanel::ColBPM,
                                  TrackTablePanel::ColKey, TrackTablePanel::ColGenre,
                                  TrackTablePanel::ColDate});
    m_recentlyAdded->setEmptyMessage("Aucune piste ajoutee recemment.");
    m_content->addAndMakeVisible(*m_recentlyAdded);

    m_recentlyAnalyzed = std::make_unique<TrackTablePanel>("RECEMMENT ANALYSEES", false);
    m_recentlyAnalyzed->addColumns({TrackTablePanel::ColTitle, TrackTablePanel::ColArtist,
                                     TrackTablePanel::ColYt, TrackTablePanel::ColBPM,
                                     TrackTablePanel::ColKey, TrackTablePanel::ColEnergy});
    m_recentlyAnalyzed->setEmptyMessage("Aucune piste analysee. Lancez l'analyse BPM/Key depuis la vue Analyse.");
    m_content->addAndMakeVisible(*m_recentlyAnalyzed);

    wireStreamingTrackActions(m_topPlayed.get(), m_provider);
    wireStreamingTrackActions(m_recentlyAdded.get(), m_provider);
    wireStreamingTrackActions(m_recentlyAnalyzed.get(), m_provider);

    m_unanalyzedBanner = std::make_unique<juce::Label>();
    m_unanalyzedBanner->setFont(juce::Font(13.0f, juce::Font::bold));
    m_unanalyzedBanner->setJustificationType(juce::Justification::centredLeft);
    m_content->addAndMakeVisible(*m_unanalyzedBanner);

    refresh();
}

void TendancesTab::visibilityChanged()
{
    if (isVisible())
        refresh();
}

void TendancesTab::refresh()
{
    if (!m_provider) return;

    auto top = m_provider->getMostPlayed(50);
    if (top.empty()) {
        top = m_provider->getAllTracks();
        if (top.size() > 50) top.resize(50);
    }
    m_topPlayed->setTracks(top);

    auto recent = m_provider->getRecentlyAdded(50);
    if (recent.empty()) {
        auto all = m_provider->getAllTracks();
        if (all.size() > 50) {
            recent.assign(all.end() - 50, all.end());
        } else {
            recent = all;
        }
    }
    m_recentlyAdded->setTracks(recent);

    auto all = m_provider->getAllTracks();
    std::vector<Models::Track> analyzed;
    analyzed.reserve(all.size());
    for (auto& t : all) {
        if (t.analyzed && t.bpm > 0.0)
            analyzed.push_back(t);
    }
    std::sort(analyzed.begin(), analyzed.end(),
              [](const Models::Track& a, const Models::Track& b) {
                  return a.analyzedDate > b.analyzedDate;
              });
    if (analyzed.size() > 50) analyzed.resize(50);
    m_recentlyAnalyzed->setTracks(analyzed);

    auto unanalyzed = m_provider->getUnanalyzedTracks();
    if (!unanalyzed.empty()) {
        m_unanalyzedBanner->setText(
            juce::String(juce::CharPointer_UTF8("\xe2\x9a\xa0 ")) + juce::String(static_cast<int>(unanalyzed.size()))
            + " piste(s) sans analyse BPM/Key. Lancez l'analyse pour enrichir votre collection !",
            juce::dontSendNotification);
        m_unanalyzedBanner->setColour(juce::Label::textColourId, Colors::warning());
        m_unanalyzedBanner->setColour(juce::Label::backgroundColourId, Colors::warning().withAlpha(0.08f));
    } else {
        m_unanalyzedBanner->setText(
            juce::String(juce::CharPointer_UTF8("\xe2\x9c\x93 ")) + juce::String("Toutes vos pistes sont analysees !"),
            juce::dontSendNotification);
        m_unanalyzedBanner->setColour(juce::Label::textColourId, Colors::success());
        m_unanalyzedBanner->setColour(juce::Label::backgroundColourId, Colors::success().withAlpha(0.08f));
    }

    resized();
}

void TendancesTab::retranslateUi()
{
    if (m_topPlayed)        m_topPlayed->retranslateUi();
    if (m_recentlyAdded)    m_recentlyAdded->retranslateUi();
    if (m_recentlyAnalyzed) m_recentlyAnalyzed->retranslateUi();
}

void TendancesTab::paint(juce::Graphics& g)
{
    ProDraw::viewBackground(g, getWidth(), getHeight());
}

void TendancesTab::resized()
{
    m_viewport->setBounds(getLocalBounds());

    auto contentW = getWidth() - 24;
    int y = 12;
    int panelGap = 16;

    int topRows = juce::jmax(3, static_cast<int>(m_topPlayed->getTracks().size()));
    int topH = 40 + topRows * 28;
    m_topPlayed->setBounds(12, y, contentW, juce::jmin(topH, 500));
    y += m_topPlayed->getHeight() + panelGap;

    m_unanalyzedBanner->setBounds(12, y, contentW, 32);
    y += 40;

    int recentRows = juce::jmax(3, static_cast<int>(m_recentlyAdded->getTracks().size()));
    int recentH = 40 + recentRows * 28;
    m_recentlyAdded->setBounds(12, y, contentW, juce::jmin(recentH, 400));
    y += m_recentlyAdded->getHeight() + panelGap;

    int analyzedRows = juce::jmax(3, static_cast<int>(m_recentlyAnalyzed->getTracks().size()));
    int analyzedH = 40 + analyzedRows * 28;
    m_recentlyAnalyzed->setBounds(12, y, contentW, juce::jmin(analyzedH, 400));
    y += m_recentlyAnalyzed->getHeight() + panelGap;

    m_content->setSize(contentW + 24, y);
}

ChartsTab::ChartsTab(Services::Library::TrackDataProvider* provider)
    : juce::Thread("ChartsLoader"), m_provider(provider)
{
    m_statusLabel = std::make_unique<juce::Label>();
    m_statusLabel->setFont(juce::Font(13.0f));
    m_statusLabel->setColour(juce::Label::textColourId, Colors::textSecondary());
    m_statusLabel->setJustificationType(juce::Justification::centred);
    m_statusLabel->setText(BM_TJ("streaming.charts.loadingMulti"), juce::dontSendNotification);
    addAndMakeVisible(*m_statusLabel);

    m_chartTable = std::make_unique<TrackTablePanel>("TOP 100", true);
    m_chartTable->addColumns({TrackTablePanel::ColRank, TrackTablePanel::ColDelta,
                              TrackTablePanel::ColTitle, TrackTablePanel::ColArtist,
                              TrackTablePanel::ColLib, TrackTablePanel::ColYt,
                              TrackTablePanel::ColBPM, TrackTablePanel::ColKey,
                              TrackTablePanel::ColGenre}, false);
    m_chartTable->setEmptyMessageKey("streaming.charts.empty");
    addAndMakeVisible(*m_chartTable);

    auto* providerPtr = m_provider;
    auto* tablePtr = m_chartTable.get();
    m_chartTable->onRowDoubleClicked = [tablePtr](int row, const Models::Track& t) {
        const auto* m = tablePtr->chartMetaFor(row);
        const std::string url = (m != nullptr) ? m->previewUrl.toStdString() : std::string{};
        playBestSource(t, url);
    };
    m_chartTable->onYoutubeClicked = [](int, const Models::Track& t) {
        openYouTubePreview(t);
    };
    m_chartTable->onRowRightClicked = [tablePtr, providerPtr](int row, const Models::Track& t,
                                                              juce::Point<int> screenPos) {
        const auto* m = tablePtr->chartMetaFor(row);
        const std::string url = (m != nullptr) ? m->previewUrl.toStdString() : std::string{};
        juce::PopupMenu menu;
        menu.addItem(1, trackHasLocalFile(t)
                            ? juce::String::fromUTF8("\xE2\x96\xB6 Ecouter (bibliotheque, complet)")
                            : juce::String::fromUTF8("\xE2\x96\xB6 Ecouter l'extrait (30s)"));
        menu.addItem(4, juce::String::fromUTF8("Preecoute YouTube"));
        menu.addItem(2, juce::String::fromUTF8("\xE2\x8F\xB9 Arreter la preecoute"));
        menu.addSeparator();
        menu.addItem(3, juce::String::fromUTF8("Ajouter a ma bibliotheque"), providerPtr != nullptr);
        const Models::Track track = t;
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
            juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
            [track, url, providerPtr](int r) {
                if (r == 1)
                    playBestSource(track, url);
                else if (r == 4)
                    openYouTubePreview(track);
                else if (r == 2)
                    stopAllPreviews();
                else if (r == 3 && providerPtr) {
                    if (track.filePath.empty()) {
                        Widgets::ToastNotifier::getInstance().show(
                            juce::String::fromUTF8("Piste streaming"),
                            juce::String::fromUTF8("Pas de fichier local : recherchez-la sur vos plateformes ou importez-la."),
                            Widgets::ToastNotifier::Kind::Warning);
                        return;
                    }
                    providerPtr->addTrack(track);
                    Widgets::ToastNotifier::getInstance().show(
                        juce::String::fromUTF8("Ajoutee a la bibliotheque"),
                        juce::String(track.artist) + " - " + juce::String(track.title),
                        Widgets::ToastNotifier::Kind::Success);
                }
            });
    };

    m_countryCombo = std::make_unique<juce::ComboBox>();
    m_countryCombo->addItem("Monde (Global)",             1);
    m_countryCombo->addItem(BM_TJ("trending.country.fr"), 2);
    m_countryCombo->addItem(BM_TJ("trending.country.us"), 3);
    m_countryCombo->addItem(BM_TJ("trending.country.gb"), 4);
    m_countryCombo->addItem(BM_TJ("trending.country.de"), 5);
    m_countryCombo->addItem(BM_TJ("trending.country.es"), 6);
    m_countryCombo->addItem(BM_TJ("trending.country.it"), 7);
    m_countryCombo->addItem(BM_TJ("trending.country.jp"), 8);
    m_countryCombo->addItem(BM_TJ("trending.country.br"), 9);
    m_countryCombo->setSelectedId(2, juce::dontSendNotification);
    m_countryCombo->onChange = [this]() {
        static const char* const codes[] = {
            "global", "fr", "us", "gb", "de", "es", "it", "jp", "br"
        };
        const int idx = m_countryCombo->getSelectedId() - 1;
        if (idx >= 0 && idx < 9) m_selectedCountry = codes[idx];
        startChartLoad(false);
    };
    addAndMakeVisible(*m_countryCombo);

    m_refreshBtn = std::make_unique<juce::TextButton>("Actualiser");
    m_refreshBtn->setColour(juce::TextButton::buttonColourId, Colors::primary());
    m_refreshBtn->setColour(juce::TextButton::buttonOnColourId, Colors::primaryHover());
    m_refreshBtn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    m_refreshBtn->setColour(juce::TextButton::textColourOnId,  juce::Colours::white);
    m_refreshBtn->onClick = [this] {
        spdlog::info("[StreamingView] refreshBtn clicked country={}", m_selectedCountry);
        startChartLoad(true);
    };
    addAndMakeVisible(*m_refreshBtn);

    startChartLoad(true);
}

void ChartsTab::startChartLoad(bool force)
{
    if (force) m_forceRefresh = true;
    if (isThreadRunning()) return;
    m_statusLabel->setText(juce::String::formatted(BM_T("streaming.charts.loadingForFmt").c_str(),
                                                   juce::String(m_selectedCountry).toUpperCase().toRawUTF8()),
                           juce::dontSendNotification);
    m_statusLabel->setVisible(true);
    startThread();
}

ChartsTab::~ChartsTab()
{
    stopThread(3000);
}

void ChartsTab::retranslateUi()
{
    if (m_countryCombo) {
        const int prev = m_countryCombo->getSelectedId();
        m_countryCombo->clear(juce::dontSendNotification);
        m_countryCombo->addItem("Monde (Global)",             1);
        m_countryCombo->addItem(BM_TJ("trending.country.fr"), 2);
        m_countryCombo->addItem(BM_TJ("trending.country.us"), 3);
        m_countryCombo->addItem(BM_TJ("trending.country.gb"), 4);
        m_countryCombo->addItem(BM_TJ("trending.country.de"), 5);
        m_countryCombo->addItem(BM_TJ("trending.country.es"), 6);
        m_countryCombo->addItem(BM_TJ("trending.country.it"), 7);
        m_countryCombo->addItem(BM_TJ("trending.country.jp"), 8);
        m_countryCombo->addItem(BM_TJ("trending.country.br"), 9);
        if (prev > 0) m_countryCombo->setSelectedId(prev, juce::dontSendNotification);
    }
    if (m_chartTable) m_chartTable->retranslateUi();
}

void ChartsTab::run()
{
    using Services::Streaming::ChartsService;
    const std::string country = m_selectedCountry.empty() ? std::string("global")
                                                          : m_selectedCountry;
    const bool force = m_forceRefresh.exchange(false);

    Services::Streaming::LiveChart chart;
    try {
        chart = ChartsService::instance().getChart(country, force);
    } catch (const std::exception& ex) {
        spdlog::warn("[ChartsTab] fetch failed: {}", ex.what());
    } catch (...) {
        spdlog::warn("[ChartsTab] fetch failed (unknown)");
    }
    spdlog::info("[ChartsTab] {} entrees pour '{}' (source='{}', chart du {})",
                 chart.entries.size(), country, chart.source, chart.chartDate);

    std::map<std::string, Models::Track> lib;
    if (m_provider != nullptr && !chart.entries.empty()) {
        try {
            for (auto& t : m_provider->getAllTracks())
                lib.emplace(ChartsService::matchKey(t.title, t.artist), t);
        } catch (...) {}
    }

    auto tracks = std::make_shared<std::vector<Models::Track>>();
    auto meta   = std::make_shared<std::vector<TrackTablePanel::ChartRowMeta>>();
    tracks->reserve(chart.entries.size());
    meta->reserve(chart.entries.size());
    for (const auto& e : chart.entries) {
        Models::Track t;
        t.title  = e.title;
        t.artist = e.artist;
        TrackTablePanel::ChartRowMeta m;
        m.delta        = e.delta;
        m.prevPosition = e.previousPosition;
        m.isNew        = e.isNew;
        m.hasDelta     = e.isNew || e.delta != 0 || e.previousPosition > 0;
        m.previewUrl   = juce::String(e.previewUrl);
        auto it = lib.find(ChartsService::matchKey(e.title, e.artist));
        if (it != lib.end()) {
            const auto& lt = it->second;
            m.inLibrary  = true;
            t.id         = lt.id;
            t.filePath   = lt.filePath;
            t.bpm        = lt.bpm;
            t.key        = lt.key;
            t.camelotKey = lt.camelotKey;
            t.genre      = lt.genre;
            t.energy     = lt.energy;
            t.analyzed   = lt.analyzed;
        }
        meta->push_back(std::move(m));
        tracks->push_back(std::move(t));
    }

    juce::String status;
    if (tracks->empty()) {
        status = chart.errorMessage.empty()
            ? juce::String("Impossible de charger les charts pour ")
              + (country == "global" ? juce::String("MONDE")
                                     : juce::String(country).toUpperCase())
              + juce::String(".\nVerifiez votre connexion Internet et reessayez "
                             "avec le bouton Actualiser.")
            : juce::String::fromUTF8(chart.errorMessage.c_str());
    } else {
        juce::String d = juce::String(chart.chartDate);
        if (d.length() >= 10)
            d = d.substring(8, 10) + "/" + d.substring(5, 7) + "/" + d.substring(0, 4);
        status = juce::String("Charts du ") + d
            + juce::String::fromUTF8(" \xe2\x80\x94 ")
            + juce::String::fromUTF8(chart.source.c_str())
            + juce::String::fromUTF8(" \xe2\x80\x94 ")
            + (country == "global" ? juce::String("MONDE")
                                   : juce::String(country).toUpperCase());
        if (chart.fromCache) status += " (cache)";
        if (!chart.errorMessage.empty())
            status += juce::String::fromUTF8(" \xc2\xb7 ")
                    + juce::String::fromUTF8(chart.errorMessage.c_str());
    }

    juce::Component::SafePointer<ChartsTab> self(this);
    juce::MessageManager::callAsync([self, tracks, meta, status]() {
        if (!self) return;
        self->m_chartTable->setTracks(*tracks);
        self->m_chartTable->setChartMeta(std::move(*meta));
        self->m_chartTable->setVisible(!tracks->empty());
        self->m_statusLabel->setText(status, juce::dontSendNotification);
        self->m_statusLabel->setVisible(true);
        self->resized();
    });
}

void ChartsTab::paint(juce::Graphics& g)
{
    ProDraw::viewBackground(g, getWidth(), getHeight());
}

void ChartsTab::resized()
{
    auto area = getLocalBounds().reduced(12);
    auto topRow = area.removeFromTop(36);
    m_refreshBtn->setBounds(topRow.removeFromRight(110).reduced(0, 4));
    topRow.removeFromRight(8);
    if (m_countryCombo)
        m_countryCombo->setBounds(topRow.removeFromRight(180).reduced(0, 4));
    m_statusLabel->setBounds(area.removeFromTop(40));

    if (m_chartTable->isVisible())
        m_chartTable->setBounds(area);
    else
        m_statusLabel->setBounds(area);
}

DecouvrirTab::DecouvrirTab(Services::Library::TrackDataProvider* provider,
                           Services::Streaming::SpotifyService* spotify)
    : m_provider(provider), m_spotify(spotify)
{
    m_viewport = std::make_unique<juce::Viewport>();
    m_content = std::make_unique<juce::Component>();
    m_viewport->setViewedComponent(m_content.get(), false);
    m_viewport->setScrollBarsShown(true, false);
    addAndMakeVisible(*m_viewport);

    m_forgottenPanel = std::make_unique<TrackTablePanel>("PISTES OUBLIEES (non jouees depuis 6+ mois)", false);
    m_forgottenPanel->addColumns({TrackTablePanel::ColTitle, TrackTablePanel::ColArtist,
                                   TrackTablePanel::ColBPM, TrackTablePanel::ColKey,
                                   TrackTablePanel::ColGenre, TrackTablePanel::ColDate,
                                   TrackTablePanel::ColYt});
    m_forgottenPanel->setEmptyMessage("Aucune piste oubliee. Toutes vos pistes ont ete jouees recemment !");
    m_content->addAndMakeVisible(*m_forgottenPanel);

    m_similarPanel = std::make_unique<TrackTablePanel>("PISTES SIMILAIRES (BPM / Key / Genre)", false);
    m_similarPanel->addColumns({TrackTablePanel::ColTitle, TrackTablePanel::ColArtist,
                                 TrackTablePanel::ColBPM, TrackTablePanel::ColKey,
                                 TrackTablePanel::ColGenre, TrackTablePanel::ColEnergy,
                                 TrackTablePanel::ColYt});
    m_similarPanel->setEmptyMessage("Jouez une piste pour obtenir des suggestions similaires.");
    m_content->addAndMakeVisible(*m_similarPanel);

    m_harmonicPanel = std::make_unique<TrackTablePanel>("MIX PARFAIT (enchainement harmonique Camelot)", true);
    m_harmonicPanel->addColumns({TrackTablePanel::ColRank, TrackTablePanel::ColTitle,
                                  TrackTablePanel::ColArtist, TrackTablePanel::ColBPM,
                                  TrackTablePanel::ColKey, TrackTablePanel::ColEnergy,
                                  TrackTablePanel::ColYt});
    m_harmonicPanel->setEmptyMessage("Analysez vos pistes (BPM/Key) pour generer un enchainement harmonique.");
    m_content->addAndMakeVisible(*m_harmonicPanel);

    wireStreamingTrackActions(m_forgottenPanel.get(), m_provider);
    wireStreamingTrackActions(m_similarPanel.get(), m_provider);
    wireStreamingTrackActions(m_harmonicPanel.get(), m_provider);

    refresh();
}

std::pair<int, char> DecouvrirTab::parseCamelotKey(const std::string& key)
{
    if (key.empty()) return {0, ' '};
    try {
        auto numPart = key.substr(0, key.size() - 1);
        char letter = key.back();
        if (letter != 'A' && letter != 'B' && letter != 'a' && letter != 'b')
            return {0, ' '};
        int num = std::stoi(numPart);
        if (num < 1 || num > 12) return {0, ' '};
        return {num, static_cast<char>(std::toupper(letter))};
    } catch (...) {
        return {0, ' '};
    }
}

bool DecouvrirTab::camelotCompatible(const std::string& keyA, const std::string& keyB)
{
    auto [numA, letA] = parseCamelotKey(keyA);
    auto [numB, letB] = parseCamelotKey(keyB);
    if (numA == 0 || numB == 0) return false;

    if (numA == numB && letA == letB) return true;
    if (letA == letB) {
        int diff = std::abs(numA - numB);
        if (diff == 1 || diff == 11) return true;
    }
    if (numA == numB && letA != letB) return true;

    return false;
}

std::vector<Models::Track> DecouvrirTab::findForgottenTracks(const std::vector<Models::Track>& all)
{
    auto now = static_cast<int64_t>(std::time(nullptr));
    auto sixMonthsAgo = now - (180LL * 86400LL);
    std::vector<Models::Track> forgotten;
    for (const auto& t : all) {
        if (t.lastPlayed > 0 && t.lastPlayed < sixMonthsAgo) {
            forgotten.push_back(t);
        } else if (t.lastPlayed == 0 && t.dateAdded > 0 && t.dateAdded < sixMonthsAgo) {
            forgotten.push_back(t);
        }
    }
    std::sort(forgotten.begin(), forgotten.end(),
              [](const Models::Track& a, const Models::Track& b) {
                  auto ta = a.lastPlayed > 0 ? a.lastPlayed : a.dateAdded;
                  auto tb = b.lastPlayed > 0 ? b.lastPlayed : b.dateAdded;
                  return ta < tb;
              });
    if (forgotten.size() > 50) forgotten.resize(50);
    return forgotten;
}

std::vector<Models::Track> DecouvrirTab::findSimilarToLast(const std::vector<Models::Track>& all)
{
    if (all.empty()) return {};

    auto lastPlayed = std::max_element(all.begin(), all.end(),
        [](const Models::Track& a, const Models::Track& b) {
            return a.lastPlayed < b.lastPlayed;
        });

    if (lastPlayed == all.end() || lastPlayed->lastPlayed == 0) {
        lastPlayed = std::find_if(all.begin(), all.end(),
            [](const Models::Track& t) { return t.bpm > 0; });
        if (lastPlayed == all.end()) return {};
    }

    auto refBpm = lastPlayed->bpm;
    auto refKey = lastPlayed->camelotKey.empty() ? lastPlayed->key : lastPlayed->camelotKey;
    auto refGenre = lastPlayed->genre;
    auto refId = lastPlayed->id;

    std::vector<std::pair<double, Models::Track>> scored;
    for (const auto& t : all) {
        if (t.id == refId) continue;
        double score = 0.0;

        if (t.bpm > 0 && refBpm > 0) {
            double bpmDiff = std::abs(t.bpm - refBpm);
            if (bpmDiff < 6.0) score += 40.0 * (1.0 - bpmDiff / 6.0);
        }

        auto tKey = t.camelotKey.empty() ? t.key : t.camelotKey;
        if (!tKey.empty() && !refKey.empty() && camelotCompatible(tKey, refKey))
            score += 35.0;

        if (!t.genre.empty() && !refGenre.empty() && t.genre == refGenre)
            score += 25.0;

        if (score > 20.0)
            scored.push_back({score, t});
    }

    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    std::vector<Models::Track> result;
    for (size_t i = 0; i < std::min(scored.size(), size_t(20)); ++i)
        result.push_back(scored[i].second);
    return result;
}

std::vector<Models::Track> DecouvrirTab::buildHarmonicChain(const std::vector<Models::Track>& pool, int maxLen)
{
    std::vector<Models::Track> withKeys;
    for (const auto& t : pool) {
        auto k = t.camelotKey.empty() ? t.key : t.camelotKey;
        auto [num, let] = parseCamelotKey(k);
        if (num > 0) withKeys.push_back(t);
    }

    if (withKeys.empty()) return {};

    std::sort(withKeys.begin(), withKeys.end(),
              [](const Models::Track& a, const Models::Track& b) { return a.bpm < b.bpm; });

    std::vector<Models::Track> chain;
    std::vector<bool> used(withKeys.size(), false);
    size_t startIdx = withKeys.size() / 2;
    chain.push_back(withKeys[startIdx]);
    used[startIdx] = true;

    while (static_cast<int>(chain.size()) < maxLen) {
        const auto& last = chain.back();
        auto lastKey = last.camelotKey.empty() ? last.key : last.camelotKey;

        int bestIdx = -1;
        double bestScore = -1.0;

        for (size_t i = 0; i < withKeys.size(); ++i) {
            if (used[i]) continue;
            auto tKey = withKeys[i].camelotKey.empty() ? withKeys[i].key : withKeys[i].camelotKey;
            if (!camelotCompatible(lastKey, tKey)) continue;

            double bpmDiff = std::abs(withKeys[i].bpm - last.bpm);
            double score = 100.0 - bpmDiff;
            if (score > bestScore) {
                bestScore = score;
                bestIdx = static_cast<int>(i);
            }
        }

        if (bestIdx < 0) break;
        chain.push_back(withKeys[static_cast<size_t>(bestIdx)]);
        used[static_cast<size_t>(bestIdx)] = true;
    }

    return chain;
}

void DecouvrirTab::refresh()
{
    if (!m_provider) return;

    auto all = m_provider->getAllTracks();
    m_forgottenPanel->setTracks(findForgottenTracks(all));
    m_similarPanel->setTracks(findSimilarToLast(all));
    m_harmonicPanel->setTracks(buildHarmonicChain(all, 10));

    resized();
}

void DecouvrirTab::retranslateUi()
{
    if (m_forgottenPanel) m_forgottenPanel->retranslateUi();
    if (m_similarPanel)   m_similarPanel->retranslateUi();
    if (m_harmonicPanel)  m_harmonicPanel->retranslateUi();
}

void DecouvrirTab::paint(juce::Graphics& g)
{
    ProDraw::viewBackground(g, getWidth(), getHeight());
}

void DecouvrirTab::resized()
{
    m_viewport->setBounds(getLocalBounds());

    auto contentW = getWidth() - 24;
    int y = 12;
    int panelGap = 16;

    int forgottenRows = juce::jmax(3, static_cast<int>(m_forgottenPanel->getTracks().size()));
    int forgottenH = 40 + forgottenRows * 28;
    m_forgottenPanel->setBounds(12, y, contentW, juce::jmin(forgottenH, 400));
    y += m_forgottenPanel->getHeight() + panelGap;

    int similarRows = juce::jmax(3, static_cast<int>(m_similarPanel->getTracks().size()));
    int similarH = 40 + similarRows * 28;
    m_similarPanel->setBounds(12, y, contentW, juce::jmin(similarH, 400));
    y += m_similarPanel->getHeight() + panelGap;

    int harmonicRows = juce::jmax(3, static_cast<int>(m_harmonicPanel->getTracks().size()));
    int harmonicH = 40 + harmonicRows * 28;
    m_harmonicPanel->setBounds(12, y, contentW, juce::jmin(harmonicH, 400));
    y += m_harmonicPanel->getHeight() + panelGap;

    m_content->setSize(contentW + 24, y);
}

ComptesTab::ComptesTab()
{
    m_titleLabel = std::make_unique<juce::Label>();
    m_titleLabel->setText(BM_TJ("streaming.accounts.title"), juce::dontSendNotification);
    m_titleLabel->setFont(juce::Font(16.0f, juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    m_infoLabel = std::make_unique<juce::Label>();
    m_infoLabel->setText(
        "BeatMate fonctionne a 100% SANS compte streaming.\n"
        "Connectez un service ci-dessous uniquement si vous souhaitez\n"
        "importer vos playlists depuis ces plateformes.",
        juce::dontSendNotification);
    m_infoLabel->setFont(juce::Font(13.0f));
    m_infoLabel->setColour(juce::Label::textColourId, Colors::textSecondary());
    addAndMakeVisible(*m_infoLabel);

    struct ServiceDef { juce::String name; juce::Colour color; };
    std::vector<ServiceDef> services = {
        {"Spotify",      juce::Colour(0xFF1DB954)},
        {"Apple Music",  juce::Colour(0xFFFC3C44)},
        {"Tidal",        juce::Colour(0xFF000000)},
        {"Deezer",       juce::Colour(0xFFA238FF)},
        {"SoundCloud",   juce::Colour(0xFFFF5500)},
        {"Beatport Link",juce::Colour(0xFF94D500)}
    };

    for (const auto& svc : services) {
        ServiceRow row;
        row.nameLabel = std::make_unique<juce::Label>();
        row.nameLabel->setText(svc.name, juce::dontSendNotification);
        row.nameLabel->setFont(juce::Font(14.0f, juce::Font::bold));
        row.nameLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
        addAndMakeVisible(*row.nameLabel);

        row.statusLabel = std::make_unique<juce::Label>();
        row.statusLabel->setText(juce::String::fromUTF8("\xe2\x97\x8b") + juce::String(" Non connecte"), juce::dontSendNotification);
        row.statusLabel->setFont(juce::Font(12.0f, juce::Font::bold));
        row.statusLabel->setColour(juce::Label::textColourId, Colors::error());
        row.statusLabel->setColour(juce::Label::backgroundColourId, Colors::error().withAlpha(0.08f));
        addAndMakeVisible(*row.statusLabel);

        row.connectBtn = std::make_unique<juce::TextButton>(BM_TJ("streaming.connect"));
        row.connectBtn->setColour(juce::TextButton::buttonColourId, svc.color.withAlpha(0.7f));
        row.connectBtn->setColour(juce::TextButton::buttonOnColourId, svc.color);
        row.connectBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        row.connectBtn->setColour(juce::TextButton::textColourOnId, Colors::textPrimary());

        auto serviceName = svc.name;
        auto* statusLbl = row.statusLabel.get();
        auto* connectBtnPtr = row.connectBtn.get();
        row.connectBtn->onClick = [serviceName, statusLbl, connectBtnPtr] {
            spdlog::info("[StreamingView] connect clicked for service: {}", serviceName.toStdString());
            juce::String authUrl;
            if (serviceName == "Spotify")
                authUrl = "https://accounts.spotify.com/authorize?response_type=code&client_id=YOUR_CLIENT_ID&redirect_uri=http://127.0.0.1:8888/callback&scope=user-read-private%20playlist-read-private";
            else if (serviceName == "Apple Music")
                authUrl = "https://appleid.apple.com/auth/authorize?response_type=code&client_id=YOUR_CLIENT_ID&redirect_uri=http://127.0.0.1:8888/callback";
            else if (serviceName == "Tidal")
                authUrl = "https://login.tidal.com/authorize?response_type=code&client_id=YOUR_CLIENT_ID&redirect_uri=http://127.0.0.1:8888/callback";
            else if (serviceName == "Deezer")
                authUrl = "https://connect.deezer.com/oauth/auth.php?app_id=YOUR_CLIENT_ID&redirect_uri=http://127.0.0.1:8888/callback&perms=basic_access";
            else if (serviceName == "SoundCloud")
                authUrl = "https://soundcloud.com/connect?client_id=YOUR_CLIENT_ID&response_type=code&redirect_uri=http://127.0.0.1:8888/callback";
            else if (serviceName == "Beatport Link")
                authUrl = "https://api.beatport.com/v4/auth/o/authorize/?response_type=code&client_id=YOUR_CLIENT_ID&redirect_uri=http://127.0.0.1:8888/callback";

            if (authUrl.isEmpty()) {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                    "Service inconnu", serviceName + " non support\xc3\xa9.", "OK");
                return;
            }

            juce::URL(authUrl).launchInDefaultBrowser();
            if (statusLbl) {
                statusLbl->setText(juce::String::fromUTF8("Authentification...\n(navigateur ouvert)"),
                                   juce::dontSendNotification);
                statusLbl->setColour(juce::Label::textColourId, Colors::warning());
            }
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                juce::String::fromUTF8("Connexion ") + serviceName,
                juce::String::fromUTF8(
                    "Le navigateur s'est ouvert sur la page d'authentification de ") + serviceName
                    + juce::String::fromUTF8(
                        ".\n\nCette version de BeatMate n'embarque pas de client_id de "
                        "d\xc3\xa9veloppeur pour ce service. Pour finaliser le lien OAuth, "
                        "ajoutez votre client_id dans Param\xc3\xa8tres > Streaming puis "
                        "relancez la connexion."),
                "OK");
        };
        addAndMakeVisible(*row.connectBtn);

        m_services.push_back(std::move(row));
    }
}

void ComptesTab::paint(juce::Graphics& g)
{
    ProDraw::viewBackground(g, getWidth(), getHeight());

    auto area = getLocalBounds().reduced(24);
    area.removeFromTop(100);
    for (size_t i = 0; i < m_services.size(); ++i) {
        auto rowArea = area.removeFromTop(52);
        g.setColour(Colors::bgCard());
        g.fillRoundedRectangle(rowArea.toFloat().reduced(0, 2), 6.0f);
        g.setColour(Colors::glassBorder());
        g.drawRoundedRectangle(rowArea.toFloat().reduced(0, 2), 6.0f, 0.5f);
        area.removeFromTop(4);
    }
}

void ComptesTab::resized()
{
    auto area = getLocalBounds().reduced(24);
    m_titleLabel->setBounds(area.removeFromTop(28));
    area.removeFromTop(8);
    m_infoLabel->setBounds(area.removeFromTop(60));
    area.removeFromTop(12);

    for (auto& svc : m_services) {
        auto rowArea = area.removeFromTop(52).reduced(12, 8);
        svc.nameLabel->setBounds(rowArea.removeFromLeft(140));
        svc.connectBtn->setBounds(rowArea.removeFromRight(110).reduced(0, 2));
        rowArea.removeFromRight(8);
        svc.statusLabel->setBounds(rowArea);
        area.removeFromTop(4);
    }
}

void ComptesTab::retranslateUi()
{
    if (m_titleLabel)
        m_titleLabel->setText(BM_TJ("streaming.accounts.title"), juce::dontSendNotification);
    for (auto& row : m_serviceRows)
        if (row.connectBtn)
            row.connectBtn->setButtonText(BM_TJ("streaming.connect"));
}

StreamingView::StreamingView() { setupUI(); }

StreamingView::StreamingView(Services::Library::TrackDataProvider* provider)
    : m_provider(provider)
{
    setupUI();
}

void StreamingView::setupUI()
{
    m_titleLabel = std::make_unique<juce::Label>("title", BM_TJ("streaming.title"));
    m_titleLabel->setFont(juce::Font(22.0f, juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    m_tabWidget = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
    m_tabWidget->setColour(juce::TabbedComponent::backgroundColourId, Colors::bgDarker());
    m_tabWidget->setTabBarDepth(34);

    m_tabWidget->addTab("Tendances", Colors::bgDarker(), new TendancesTab(m_provider), true);

    m_tabWidget->addTab("Charts", Colors::bgDarker(), new ChartsTab(m_provider), true);

    m_tabWidget->addTab("Decouvrir", Colors::bgDarker(), new DecouvrirTab(m_provider), true);

    m_tabWidget->addTab("Historique", Colors::bgDarker(), new HistoriqueTab(m_provider), true);

    m_tabWidget->addTab("Comptes", Colors::bgDarker(), new ComptesTab(), true);

    addAndMakeVisible(*m_tabWidget);

    retranslateUi();
}

void StreamingView::retranslateUi()
{
    if (m_titleLabel)
        m_titleLabel->setText(BM_TJ("streaming.title"), juce::dontSendNotification);

    if (m_tabWidget) {
        for (int i = 0; i < m_tabWidget->getNumTabs(); ++i)
            if (auto* r = dynamic_cast<BeatMate::UI::IRetranslatable*>(m_tabWidget->getTabContentComponent(i)))
                r->retranslateUi();
    }

    repaint();
}

void StreamingView::onSearch()
{
    m_listeners.call([](Listener& l) { l.searchRequested("", ""); });
}

void StreamingView::onImportTrack()
{
    if (!m_provider) return;

    auto chooser = std::make_shared<juce::FileChooser>(
        "Importer un fichier audio",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        "*.mp3;*.wav;*.flac;*.aiff;*.m4a;*.ogg;*.wma");

    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
               | juce::FileBrowserComponent::canSelectMultipleItems;

    chooser->launchAsync(flags, [this, chooser](const juce::FileChooser& fc)
    {
        auto results = fc.getResults();
        int imported = 0;
        for (auto& file : results)
        {
            if (file.existsAsFile())
            {
                Models::Track track;
                track.filePath = file.getFullPathName().toStdString();
                track.title = file.getFileNameWithoutExtension().toStdString();
                m_provider->addTrack(track);
                ++imported;
            }
        }
        if (imported > 0)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                "Import", juce::String(imported) + " piste(s) importee(s) dans la bibliotheque.");
        }
    });
}

void StreamingView::paint(juce::Graphics& g)
{
    ProDraw::viewBackground(g, getWidth(), getHeight());

    int margin = 24;
    int titleY = 12;

    g.setColour(Colors::primary());
    g.fillRoundedRectangle((float)margin - 12.0f, (float)titleY + 6.0f, 3.0f, 28.0f, 1.5f);

    float headerBottom = 50.0f;
    juce::ColourGradient lineGrad(Colors::primary().withAlpha(0.4f), (float)margin, headerBottom,
                                  juce::Colours::transparentBlack, (float)(getWidth() - margin), headerBottom, false);
    g.setGradientFill(lineGrad);
    g.fillRect((float)margin, headerBottom, (float)(getWidth() - margin * 2), 1.0f);

    ProDraw::vignette(g, (float)getWidth(), (float)getHeight());
}

void StreamingView::resized()
{
    auto area = getLocalBounds();
    m_titleLabel->setBounds(area.removeFromTop(50).reduced(24, 12));
    m_tabWidget->setBounds(area.reduced(12, 0));
}

} // namespace BeatMate::UI
