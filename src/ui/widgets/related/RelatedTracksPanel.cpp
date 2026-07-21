#include "RelatedTracksPanel.h"

#include <algorithm>
#include <cctype>

#include "../../../services/library/TrackDataProvider.h"
#include "../../../services/preparation/CamelotMoveClassifier.h"

namespace BeatMate::UI::Widgets {

class RelatedTracksPanel::ResultsListModel : public juce::ListBoxModel
{
public:
    explicit ResultsListModel(RelatedTracksPanel& owner) : m_owner(owner) {}

    int getNumRows() override {
        return (int)m_owner.m_results.size();
    }

    void paintListBoxItem(int rowNumber, juce::Graphics& g,
                          int width, int height, bool rowIsSelected) override {
        if (rowNumber < 0 || rowNumber >= (int)m_owner.m_results.size()) return;
        const auto& t = m_owner.m_results[(size_t)rowNumber];

        g.fillAll(rowIsSelected
            ? juce::Colour::fromRGB(55, 65, 90)
            : juce::Colour::fromRGB(28, 32, 44));

        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
        const int pad = 8;
        juce::String left = juce::String(t.title.empty() ? t.filePath : t.title);
        if (!t.artist.empty()) left = juce::String(t.artist) + " - " + left;
        g.drawText(left, pad, 0, width - 160, height, juce::Justification::centredLeft, true);

        juce::String right;
        if (t.bpm > 0.0)         right += juce::String(t.bpm, 1) + " BPM";
        if (!t.camelotKey.empty()) right += juce::String("  ") + juce::String(t.camelotKey);
        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.drawText(right, width - 150 - pad, 0, 150, height,
                   juce::Justification::centredRight, true);
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override {
        if (row < 0 || row >= (int)m_owner.m_results.size()) return;
        if (m_owner.onTrackChosen)
            m_owner.onTrackChosen(m_owner.m_results[(size_t)row]);
    }

private:
    RelatedTracksPanel& m_owner;
};


RelatedTracksPanel::RelatedTracksPanel(Services::Library::TrackDataProvider* provider)
    : m_provider(provider)
{
    m_titleLabel = std::make_unique<juce::Label>("title", "Related Tracks");
    m_titleLabel->setFont(juce::Font(juce::FontOptions{}.withHeight(14.0f).withStyle("Bold")));
    m_titleLabel->setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(*m_titleLabel);

    m_tabs = std::make_unique<juce::TabbedButtonBar>(juce::TabbedButtonBar::TabsAtTop);
    m_tabs->addTab("BPM",       juce::Colour::fromRGB(55, 65, 90), (int)Relation::SameBPM);
    m_tabs->addTab("Key",       juce::Colour::fromRGB(55, 75, 90), (int)Relation::Compatible);
    m_tabs->addTab("Genre",     juce::Colour::fromRGB(65, 70, 90), (int)Relation::SameGenre);
    m_tabs->addTab("Artist",    juce::Colour::fromRGB(60, 70, 95), (int)Relation::SameArtist);
    m_tabs->addTab("Era",       juce::Colour::fromRGB(65, 60, 85), (int)Relation::SameEra);
    addAndMakeVisible(*m_tabs);

    m_listModel = std::make_unique<ResultsListModel>(*this);
    m_listBox = std::make_unique<juce::ListBox>("related", m_listModel.get());
    m_listBox->setRowHeight(22);
    m_listBox->setColour(juce::ListBox::backgroundColourId,
                         juce::Colour::fromRGB(24, 28, 40));
    addAndMakeVisible(*m_listBox);

    m_tabs->addChangeListener(nullptr);
    m_tabs->getTabButton(0)->onClick = [this]{ refreshFor(Relation::SameBPM);    };
    m_tabs->getTabButton(1)->onClick = [this]{ refreshFor(Relation::Compatible); };
    m_tabs->getTabButton(2)->onClick = [this]{ refreshFor(Relation::SameGenre);  };
    m_tabs->getTabButton(3)->onClick = [this]{ refreshFor(Relation::SameArtist); };
    m_tabs->getTabButton(4)->onClick = [this]{ refreshFor(Relation::SameEra);    };
}

RelatedTracksPanel::~RelatedTracksPanel() = default;

void RelatedTracksPanel::setReferenceTrack(const Models::Track& track) {
    m_reference = track;
    m_hasReference = true;
    refreshFor(m_currentTab);
}

void RelatedTracksPanel::clear() {
    m_hasReference = false;
    m_results.clear();
    if (m_listBox) m_listBox->updateContent();
    repaint();
}

void RelatedTracksPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(22, 26, 36));
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f, 1.0f);

    if (!m_hasReference) {
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
        g.drawText("Select a track to see related suggestions",
                   getLocalBounds().reduced(10),
                   juce::Justification::centred, true);
    }
}

void RelatedTracksPanel::resized() {
    auto bounds = getLocalBounds().reduced(6);
    m_titleLabel->setBounds(bounds.removeFromTop(20));
    m_tabs->setBounds(bounds.removeFromTop(28));
    bounds.removeFromTop(4);
    m_listBox->setBounds(bounds);
}

void RelatedTracksPanel::refreshFor(Relation r) {
    m_currentTab = r;
    m_results = filterByRelation(r);
    if (m_listBox) m_listBox->updateContent();
    repaint();
}

static juce::String toLowerStr(const std::string& s) {
    juce::String out = juce::String(s);
    return out.toLowerCase();
}

std::vector<Models::Track>
RelatedTracksPanel::filterByRelation(Relation r) const {
    std::vector<Models::Track> out;
    if (!m_provider || !m_hasReference) return out;

    auto all = m_provider->getAllTracks();
    Services::Preparation::CamelotMoveClassifier classifier;

    for (const auto& t : all) {
        if (t.id == m_reference.id) continue;

        switch (r) {
            case Relation::SameBPM: {
                if (m_reference.bpm <= 0.0 || t.bpm <= 0.0) break;
                double pct = std::abs(t.bpm - m_reference.bpm) / m_reference.bpm;
                if (pct <= 0.03) out.push_back(t);
                break;
            }
            case Relation::Compatible: {
                if (m_reference.camelotKey.empty() || t.camelotKey.empty()) break;
                auto cls = classifier.classify(m_reference.camelotKey, t.camelotKey);
                using M = Services::Preparation::CamelotMove;
                if (cls.move == M::Same || cls.move == M::PlusOne
                    || cls.move == M::MinusOne || cls.move == M::Relative
                    || cls.move == M::EnergyBoost || cls.move == M::Dominant)
                    out.push_back(t);
                break;
            }
            case Relation::SameGenre: {
                if (m_reference.genre.empty() || t.genre.empty()) break;
                if (toLowerStr(t.genre) == toLowerStr(m_reference.genre))
                    out.push_back(t);
                break;
            }
            case Relation::SameArtist: {
                if (m_reference.artist.empty() || t.artist.empty()) break;
                if (toLowerStr(t.artist) == toLowerStr(m_reference.artist))
                    out.push_back(t);
                break;
            }
            case Relation::SameEra: {
                if (m_reference.year <= 0 || t.year <= 0) break;
                if (std::abs(t.year - m_reference.year) <= 2)
                    out.push_back(t);
                break;
            }
        }
    }

    // Sort: BPM -> closer first; Compatible -> by camelot distance; else title
    if (r == Relation::SameBPM) {
        std::sort(out.begin(), out.end(),
            [&](const Models::Track& a, const Models::Track& b) {
                return std::abs(a.bpm - m_reference.bpm)
                     < std::abs(b.bpm - m_reference.bpm);
            });
    } else {
        std::sort(out.begin(), out.end(),
            [](const Models::Track& a, const Models::Track& b) {
                return a.title < b.title;
            });
    }

    if (out.size() > 100) out.resize(100);
    return out;
}

} // namespace BeatMate::UI::Widgets
