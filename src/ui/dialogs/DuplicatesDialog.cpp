#include "DuplicatesDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/library/TrackDataProvider.h"
#include "../../app/Application.h"
#include "../../app/ServiceLocator.h"

#include <spdlog/spdlog.h>

extern BeatMate::ServiceLocator* g_serviceLocator;

namespace BeatMate::UI {

DuplicatesDialog::DuplicatesDialog(Services::Library::TrackDataProvider* provider)
    : provider_(provider),
      scanBtn_("Analyser"),
      mergeBtn_("Fusionner la selection")
{
    methodCombo_.addItem("Par nom de fichier", 1);
    methodCombo_.addItem("Par metadonnees (titre/artiste/duree)", 2);
    methodCombo_.addItem("Par empreinte audio", 3);
    methodCombo_.setSelectedId(2, juce::dontSendNotification);
    addAndMakeVisible(methodCombo_);

    status_.setText("Choisissez une methode puis cliquez sur Analyser.",
                    juce::dontSendNotification);
    status_.setColour(juce::Label::textColourId, Colors::textSecondary());
    addAndMakeVisible(status_);

    list_.setModel(this);
    list_.setRowHeight(44);
    list_.setColour(juce::ListBox::backgroundColourId, Colors::bgDarker());
    addAndMakeVisible(list_);

    scanBtn_.setColour(juce::TextButton::buttonColourId, Colors::primary());
    scanBtn_.onClick = [this] { startScan(); };
    addAndMakeVisible(scanBtn_);

    mergeBtn_.setColour(juce::TextButton::buttonColourId, Colors::success());
    mergeBtn_.setEnabled(false);
    mergeBtn_.setTooltip("Fusionne cues/note/playlists vers la piste conservee puis retire le doublon "
                         "de la bibliotheque (le fichier audio n'est jamais supprime du disque).");
    mergeBtn_.onClick = [this] { mergeSelected(); };
    addAndMakeVisible(mergeBtn_);

    setSize(860, 540);
}

void DuplicatesDialog::show(Services::Library::TrackDataProvider* provider)
{
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(new DuplicatesDialog(provider));
    opts.dialogTitle = "Doublons de la bibliotheque";
    opts.dialogBackgroundColour = Colors::bgDarkest();
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = true;
    opts.launchAsync();
}

void DuplicatesDialog::paint(juce::Graphics& g)
{
    g.fillAll(Colors::bgDarkest());
}

void DuplicatesDialog::resized()
{
    auto area = getLocalBounds().reduced(12);
    auto top = area.removeFromTop(30);
    methodCombo_.setBounds(top.removeFromLeft(280));
    top.removeFromLeft(8);
    scanBtn_.setBounds(top.removeFromLeft(120));
    top.removeFromLeft(8);
    mergeBtn_.setBounds(top.removeFromRight(200));
    area.removeFromTop(6);
    status_.setBounds(area.removeFromTop(24));
    area.removeFromTop(6);
    list_.setBounds(area);
}

int DuplicatesDialog::getNumRows() { return (int) rows_.size(); }

void DuplicatesDialog::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool)
{
    if (row < 0 || row >= (int) rows_.size()) return;
    const auto& r = rows_[(size_t) row];

    g.fillAll(row % 2 == 0 ? Colors::bgDarker() : Colors::bgDark());

    g.setColour(Colors::border());
    g.drawRoundedRectangle(8.0f, h / 2.0f - 8.0f, 16.0f, 16.0f, 3.0f, 1.0f);
    if (r.selected) {
        g.setColour(Colors::success());
        g.fillRoundedRectangle(10.0f, h / 2.0f - 6.0f, 12.0f, 12.0f, 2.0f);
    }

    g.setFont(juce::Font(11.5f, juce::Font::bold));
    g.setColour(Colors::success());
    g.drawText("GARDER  " + juce::String(r.keep.artist) + " - " + juce::String(r.keep.title)
               + "   [" + juce::String(r.keep.filePath) + "]",
               34, 3, w - 40, 16, juce::Justification::centredLeft, true);
    g.setColour(Colors::error().withAlpha(0.85f));
    g.drawText("RETIRER " + juce::String(r.remove.artist) + " - " + juce::String(r.remove.title)
               + "   [" + juce::String(r.remove.filePath) + "]",
               34, 21, w - 40, 16, juce::Justification::centredLeft, true);

    g.setFont(juce::Font(10.0f));
    g.setColour(Colors::textMuted());
    g.drawText(juce::String((int) std::round(r.confidence * 100)) + " %",
               w - 56, 0, 48, h, juce::Justification::centredRight);
}

void DuplicatesDialog::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= (int) rows_.size()) return;
    rows_[(size_t) row].selected = !rows_[(size_t) row].selected;
    list_.repaintRow(row);
}

void DuplicatesDialog::startScan()
{
    if (scanning_) return;
    auto* pool = BeatMate::getBackgroundPool();
    auto* detector = g_serviceLocator
        ? g_serviceLocator->tryGet<Services::Library::DuplicateDetector>()
        : nullptr;
    if (!pool || !detector) {
        status_.setText("Service de detection indisponible.", juce::dontSendNotification);
        return;
    }

    scanning_ = true;
    scanBtn_.setEnabled(false);
    mergeBtn_.setEnabled(false);
    status_.setText("Analyse en cours...", juce::dontSendNotification);

    Services::Library::DuplicateMethod method = Services::Library::DuplicateMethod::ByMetadata;
    if (methodCombo_.getSelectedId() == 1) method = Services::Library::DuplicateMethod::ByFilename;
    if (methodCombo_.getSelectedId() == 3) method = Services::Library::DuplicateMethod::ByFingerprint;

    juce::Component::SafePointer<DuplicatesDialog> self(this);
    pool->addJob([self, detector, method]() {
        auto pairs = detector->findDuplicates(method);
        std::vector<PairRow> rows;
        rows.reserve(pairs.size());
        for (auto& p : pairs) {
            PairRow r;
            const bool exists1 = juce::File(juce::String(p.track1.filePath)).existsAsFile();
            const bool exists2 = juce::File(juce::String(p.track2.filePath)).existsAsFile();
            bool keepFirst = true;
            if (exists1 != exists2) keepFirst = exists1;
            else if (p.track1.fileSize != p.track2.fileSize) keepFirst = p.track1.fileSize > p.track2.fileSize;
            else keepFirst = p.track1.id < p.track2.id;
            r.keep = keepFirst ? p.track1 : p.track2;
            r.remove = keepFirst ? p.track2 : p.track1;
            r.confidence = p.confidence;
            rows.push_back(std::move(r));
        }
        juce::MessageManager::callAsync([self, rows = std::move(rows)]() mutable {
            if (!self) return;
            self->scanning_ = false;
            self->rows_ = std::move(rows);
            self->scanBtn_.setEnabled(true);
            self->mergeBtn_.setEnabled(!self->rows_.empty());
            self->status_.setText(self->rows_.empty()
                ? juce::String("Aucun doublon detecte.")
                : juce::String((int) self->rows_.size())
                    + " paire(s) de doublons. Cliquez sur une ligne pour (de)cocher, puis fusionnez.",
                juce::dontSendNotification);
            self->list_.updateContent();
            self->list_.repaint();
        });
    });
}

void DuplicatesDialog::mergeSelected()
{
    if (!provider_) return;
    int merged = 0;
    provider_->beginBatch();
    for (const auto& r : rows_) {
        if (!r.selected || r.keep.id <= 0 || r.remove.id <= 0 || r.keep.id == r.remove.id)
            continue;

        auto keep = provider_->getTrack(r.keep.id);
        const auto removeId = r.remove.id;

        for (const auto& cue : provider_->getCuePoints(removeId)) {
            auto copy = cue;
            copy.id = 0;
            copy.trackId = keep.id;
            provider_->saveCuePoint(copy);
        }
        if (keep.rating <= 0 && r.remove.rating > 0) {
            keep.rating = r.remove.rating;
            provider_->updateTrack(keep);
        }
        for (const auto& pl : provider_->getAllPlaylists()) {
            const auto tracks = provider_->getPlaylistTracks(pl.id);
            for (const auto& t : tracks) {
                if (t.id == removeId) {
                    provider_->addToPlaylist(pl.id, keep.id);
                    provider_->removeFromPlaylist(pl.id, removeId);
                    break;
                }
            }
        }
        provider_->deleteTrack(removeId);
        ++merged;
    }
    provider_->endBatch();
    status_.setText(juce::String(merged) + " doublon(s) fusionne(s) et retire(s) de la bibliotheque.",
                    juce::dontSendNotification);
    spdlog::info("[Duplicates] merged {} pair(s)", merged);
    startScan();
}

} // namespace BeatMate::UI
