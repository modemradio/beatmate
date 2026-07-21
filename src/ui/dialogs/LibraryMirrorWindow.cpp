#include "LibraryMirrorWindow.h"
#include "../styles/ColorPalette.h"
#include "../../services/library/TrackDataProvider.h"

#include <spdlog/spdlog.h>

namespace BeatMate::UI {

class LibraryMirrorWindow::Content : public juce::Component,
                                     private juce::ListBoxModel {
public:
    Content(Services::Library::TrackDataProvider* provider,
            std::function<void(int64_t)> onChosen)
        : provider_(provider), onChosen_(std::move(onChosen))
    {
        search_.setTextToShowWhenEmpty("Rechercher un morceau...", Colors::textMuted());
        search_.setColour(juce::TextEditor::backgroundColourId, Colors::bgMedium());
        search_.setColour(juce::TextEditor::textColourId, Colors::textPrimary());
        search_.setColour(juce::TextEditor::outlineColourId, Colors::border());
        search_.onTextChange = [this] { applyFilter(); };
        addAndMakeVisible(search_);

        count_.setFont(juce::Font(11.0f));
        count_.setColour(juce::Label::textColourId, Colors::textMuted());
        addAndMakeVisible(count_);

        list_.setModel(this);
        list_.setRowHeight(30);
        list_.setColour(juce::ListBox::backgroundColourId, Colors::bgDarker());
        addAndMakeVisible(list_);

        reload();
    }

    void reload()
    {
        all_.clear();
        if (provider_) all_ = provider_->getAllTracks();
        applyFilter();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(8);
        auto top = r.removeFromTop(28);
        count_.setBounds(top.removeFromRight(90));
        top.removeFromRight(6);
        search_.setBounds(top);
        r.removeFromTop(6);
        list_.setBounds(r);
    }

    void paint(juce::Graphics& g) override { g.fillAll(Colors::bgDarkest()); }

private:
    void applyFilter()
    {
        const juce::String q = search_.getText().trim().toLowerCase();
        filtered_.clear();
        filtered_.reserve(all_.size());
        for (const auto& t : all_) {
            if (q.isNotEmpty()) {
                const juce::String hay = (juce::String(t.artist) + " " + juce::String(t.title)).toLowerCase();
                if (!hay.contains(q)) continue;
            }
            filtered_.push_back(&t);
        }
        list_.updateContent();
        list_.repaint();
        count_.setText(juce::String((int) filtered_.size()) + " morceaux", juce::dontSendNotification);
    }

    int getNumRows() override { return (int) filtered_.size(); }

    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= (int) filtered_.size()) return;
        const auto& t = *filtered_[(size_t) row];
        if (selected) g.fillAll(Colors::primary().withAlpha(0.25f));
        else if (row % 2 == 0) g.fillAll(Colors::bgDarker().withAlpha(0.4f));

        g.setColour(selected ? juce::Colours::white : Colors::textPrimary());
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText(juce::String(t.title), 8, 2, w - 120, h - 4, juce::Justification::centredLeft, true);

        g.setFont(juce::Font(11.0f));
        g.setColour(Colors::textSecondary());
        g.drawText(juce::String(t.artist), 8, 2, w - 120, h - 4, juce::Justification::bottomLeft, true);

        if (t.bpm > 0) {
            g.setColour(juce::Colour(0xFF3B82F6));
            g.setFont(juce::Font("Consolas", 11.0f, juce::Font::bold));
            g.drawText(juce::String(t.bpm, 0), w - 110, 2, 44, h - 4, juce::Justification::centredRight);
        }
        const juce::String key = t.camelotKey.empty() ? juce::String(t.key) : juce::String(t.camelotKey);
        if (key.isNotEmpty()) {
            g.setColour(juce::Colour(0xFF8B5CF6));
            g.setFont(juce::Font("Consolas", 11.0f, juce::Font::bold));
            g.drawText(key, w - 58, 2, 50, h - 4, juce::Justification::centredRight);
        }
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override
    {
        if (row < 0 || row >= (int) filtered_.size()) return;
        if (onChosen_) onChosen_(filtered_[(size_t) row]->id);
    }

    Services::Library::TrackDataProvider* provider_ = nullptr;
    std::function<void(int64_t)> onChosen_;
    std::vector<Models::Track> all_;
    std::vector<const Models::Track*> filtered_;

    juce::TextEditor search_;
    juce::Label count_;
    juce::ListBox list_;
};

LibraryMirrorWindow::LibraryMirrorWindow(Services::Library::TrackDataProvider* provider,
                                         std::function<void(int64_t)> onTrackChosen)
    : juce::DocumentWindow("Bibliotheque", Colors::bgDarkest(),
                           juce::DocumentWindow::closeButton)
{
    content_ = std::make_unique<Content>(provider, std::move(onTrackChosen));
    setUsingNativeTitleBar(true);
    setResizable(true, false);
    setContentNonOwned(content_.get(), false);
    centreWithSize(440, 620);
    setAlwaysOnTop(true);
    setVisible(true);
}

LibraryMirrorWindow::~LibraryMirrorWindow()
{
    clearContentComponent();
}

void LibraryMirrorWindow::refresh()
{
    if (content_) content_->reload();
}

void LibraryMirrorWindow::closeButtonPressed()
{
    if (onClosed) onClosed();
}

} // namespace BeatMate::UI
