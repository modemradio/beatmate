#include "RelinkDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/library/TrackDataProvider.h"
#include "../../app/Application.h"

#include <spdlog/spdlog.h>

namespace BeatMate::UI {

RelinkDialog::RelinkDialog(Services::Library::TrackDataProvider* provider)
    : provider_(provider), service_(provider),
      scanBtn_("Rechercher les fichiers manquants"),
      addRootBtn_("Chercher dans..."),
      applyBtn_("Relier la selection")
{
    status_.setText("Cliquez sur Rechercher pour analyser la bibliotheque.",
                    juce::dontSendNotification);
    status_.setColour(juce::Label::textColourId, Colors::textSecondary());
    addAndMakeVisible(status_);

    list_.setModel(this);
    list_.setRowHeight(40);
    list_.setColour(juce::ListBox::backgroundColourId, Colors::bgDarker());
    addAndMakeVisible(list_);

    scanBtn_.setColour(juce::TextButton::buttonColourId, Colors::primary());
    scanBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    scanBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    scanBtn_.onClick = [this] { startScan(); };
    addAndMakeVisible(scanBtn_);

    addRootBtn_.setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    addRootBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    addRootBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    addRootBtn_.onClick = [this] {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Dossier ou chercher les fichiers deplaces", juce::File{});
        chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectDirectories,
            [this, chooser](const juce::FileChooser& fc) {
                auto dir = fc.getResult();
                if (dir.isDirectory()) {
                    roots_.push_back(dir.getFullPathName().toStdString());
                    startScan();
                }
            });
    };
    addAndMakeVisible(addRootBtn_);

    applyBtn_.setColour(juce::TextButton::buttonColourId, Colors::success());
    applyBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    applyBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    applyBtn_.setEnabled(false);
    applyBtn_.onClick = [this] { applySelected(); };
    addAndMakeVisible(applyBtn_);

    setSize(820, 520);
    startScan();
}

void RelinkDialog::show(Services::Library::TrackDataProvider* provider)
{
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(new RelinkDialog(provider));
    opts.dialogTitle = "Relier les fichiers manquants";
    opts.dialogBackgroundColour = Colors::bgDarkest();
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = true;
    opts.launchAsync();
}

void RelinkDialog::paint(juce::Graphics& g)
{
    g.fillAll(Colors::bgDarkest());
}

void RelinkDialog::resized()
{
    auto area = getLocalBounds().reduced(12);
    auto top = area.removeFromTop(30);
    scanBtn_.setBounds(top.removeFromLeft(240));
    top.removeFromLeft(8);
    addRootBtn_.setBounds(top.removeFromLeft(160));
    top.removeFromLeft(8);
    applyBtn_.setBounds(top.removeFromRight(180));
    area.removeFromTop(6);
    status_.setBounds(area.removeFromTop(24));
    area.removeFromTop(6);
    list_.setBounds(area);
}

int RelinkDialog::getNumRows() { return (int) candidates_.size(); }

void RelinkDialog::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool)
{
    if (row < 0 || row >= (int) candidates_.size()) return;
    const auto& c = candidates_[(size_t) row];

    g.fillAll(row % 2 == 0 ? Colors::bgDarker() : Colors::bgDark());

    g.setColour(Colors::border());
    g.drawRoundedRectangle(8.0f, h / 2.0f - 8.0f, 16.0f, 16.0f, 3.0f, 1.0f);
    if (c.accepted) {
        g.setColour(Colors::success());
        g.fillRoundedRectangle(10.0f, h / 2.0f - 6.0f, 12.0f, 12.0f, 2.0f);
    }

    g.setColour(Colors::textPrimary());
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    const juce::String head = (c.artist.empty() ? juce::String(c.title)
                                                : juce::String(c.artist) + " - " + juce::String(c.title))
                              + "   (" + juce::String((int) std::round(c.confidence * 100)) + " %)";
    g.drawText(head, 34, 2, w - 40, 16, juce::Justification::centredLeft, true);

    g.setFont(juce::Font(10.5f));
    g.setColour(Colors::error().withAlpha(0.8f));
    g.drawText(juce::String(c.oldPath), 34, 18, w - 40, 11, juce::Justification::centredLeft, true);
    g.setColour(Colors::success().withAlpha(0.9f));
    g.drawText(juce::String(c.newPath), 34, 29, w - 40, 11, juce::Justification::centredLeft, true);
}

void RelinkDialog::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= (int) candidates_.size()) return;
    candidates_[(size_t) row].accepted = !candidates_[(size_t) row].accepted;
    list_.repaintRow(row);
}

void RelinkDialog::startScan()
{
    if (scanning_) return;
    auto* pool = BeatMate::getBackgroundPool();
    if (!pool || !provider_) return;

    scanning_ = true;
    scanBtn_.setEnabled(false);
    applyBtn_.setEnabled(false);
    status_.setText("Analyse de la bibliotheque en cours...", juce::dontSendNotification);

    juce::Component::SafePointer<RelinkDialog> self(this);
    auto extraRoots = roots_;
    pool->addJob([self, extraRoots]() {
        if (!self) return;
        auto* svc = &self->service_;
        auto missing = svc->findMissingTracks();
        std::vector<Services::Library::RelinkCandidate> found;
        if (!missing.empty()) {
            auto roots = svc->defaultSearchRoots();
            roots.insert(roots.end(), extraRoots.begin(), extraRoots.end());
            found = svc->findCandidates(missing, roots);
        }
        const int missingCount = (int) missing.size();
        juce::MessageManager::callAsync([self, missingCount, found = std::move(found)]() mutable {
            if (!self) return;
            self->scanning_ = false;
            self->missingCount_ = missingCount;
            self->candidates_ = std::move(found);
            self->scanBtn_.setEnabled(true);
            self->applyBtn_.setEnabled(!self->candidates_.empty());
            if (missingCount == 0)
                self->status_.setText("Aucun fichier manquant : toute la bibliotheque est valide.",
                                      juce::dontSendNotification);
            else
                self->status_.setText(juce::String(missingCount) + " fichier(s) manquant(s), "
                                      + juce::String((int) self->candidates_.size())
                                      + " correspondance(s) trouvee(s). Cochez puis appliquez.",
                                      juce::dontSendNotification);
            self->list_.updateContent();
            self->list_.repaint();
        });
    });
}

void RelinkDialog::applySelected()
{
    const int applied = service_.applyRelinks(candidates_);
    status_.setText(juce::String(applied) + " piste(s) reliee(s).", juce::dontSendNotification);
    startScan();
}

}
