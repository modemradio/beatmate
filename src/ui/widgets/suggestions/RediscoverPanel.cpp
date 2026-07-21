#include "RediscoverPanel.h"

#include "../../utils/AutoCloseAlert.h"
#include "../../styles/ColorPalette.h"
#include "../../../services/config/I18n.h"

#include "LivePreviewPlayer.h"
#include "../../../services/library/TrackDatabase.h"
#include "../../../services/djsoftware/SendToDJRouter.h"
#include "../../../services/suggestions/SmartSuggestEngine.h"
#include "../../../services/preparation/CamelotMoveClassifier.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>

namespace BeatMate::UI::Widgets {

using Services::Library::TrackDatabase;
using Services::DJSoftware::DJTarget;
using Services::DJSoftware::DeckSlot;
using Services::DJSoftware::SendToDJRouter;
using Services::Suggestions::SmartSuggestEngine;
using Services::Preparation::CamelotMoveClassifier;
using Services::Preparation::CamelotMove;

namespace {

constexpr int kRowHeight      = 58;
constexpr int kRowSpacing     = 3;
constexpr int kAutoRefreshMs  = 5 * 60 * 1000;

juce::Font bodyFont(float size, bool bold = false) {
    return juce::Font(juce::FontOptions().withHeight(size)
                      .withStyle(bold ? "Bold" : "Regular"));
}

int64_t nowSeconds() {
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

juce::String addedAgo(int64_t dateAddedUnix) {
    if (dateAddedUnix <= 0) return juce::String::fromUTF8("ajout\xc3\xa9 -");
    const int64_t days = std::max<int64_t>(0, (nowSeconds() - dateAddedUnix) / 86400);
    if (days <= 0)  return juce::String::fromUTF8("ajout\xc3\xa9 aujourd'hui");
    if (days == 1)  return juce::String::fromUTF8("ajout\xc3\xa9 il y a 1 jour");
    return juce::String::fromUTF8("ajout\xc3\xa9 il y a ") + juce::String((long long) days) + " jours";
}

bool isCompatibleMove(CamelotMove m) {
    switch (m) {
        case CamelotMove::Same:
        case CamelotMove::Relative:
        case CamelotMove::PlusOne:
        case CamelotMove::MinusOne:
        case CamelotMove::EnergyBoost:
        case CamelotMove::Dominant:
        case CamelotMove::MoodShift:
            return true;
        default:
            return false;
    }
}

}

class RediscoverPanel::Row : public juce::Component {
public:
    Row(Models::Track track,
        SendToDJRouter* router)
      : track_(std::move(track)), router_(router)
    {
        sendBtn_ = std::make_unique<juce::TextButton>("Envoyer");
        sendBtn_->setColour(juce::TextButton::buttonColourId, Colors::primary());
        sendBtn_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        sendBtn_->onClick = [this] { showSendMenu(); };
        addAndMakeVisible(*sendBtn_);
    }

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(Colors::bgCard());
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(Colors::borderLight());
        g.drawRoundedRectangle(r, 5.0f, 1.0f);

        auto area = r.reduced(8.0f, 6.0f);
        area.removeFromRight(260.0f);

        auto topLine = area.removeFromTop(18.0f);
        g.setColour(Colors::textPrimary());
        g.setFont(bodyFont(13.0f, true));
        g.drawFittedText(juce::String(track_.title),
                         topLine.toNearestInt(),
                         juce::Justification::centredLeft, 1);

        auto midLine = area.removeFromTop(15.0f);
        g.setColour(Colors::textSecondary());
        g.setFont(bodyFont(11.0f));
        juce::String mid = juce::String(track_.artist);
        if (track_.bpm > 0.0)
            mid += "  -  " + juce::String(track_.bpm, 1) + " BPM";
        const auto camelot = !track_.camelotKey.empty() ? track_.camelotKey : track_.key;
        if (!camelot.empty())
            mid += "  -  " + juce::String(camelot);
        g.drawFittedText(mid, midLine.toNearestInt(),
                         juce::Justification::centredLeft, 1);

        auto botLine = area;
        g.setColour(Colors::textMuted());
        g.setFont(bodyFont(10.5f));
        juce::String bot = addedAgo(track_.dateAdded);
        bot += "  -  " + juce::String(track_.playCount) + " lecture"
             + (track_.playCount == 1 ? juce::String() : juce::String("s"));
        g.drawFittedText(bot, botLine.toNearestInt(),
                         juce::Justification::centredLeft, 1);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(6, 6);
        auto btns = r.removeFromRight(120);
        sendBtn_->setBounds(btns.reduced(2, 4));
    }

private:
    void showSendMenu() {
        const auto query = (juce::String(track_.artist) + " " + juce::String(track_.title)).trim();
        if (query.isEmpty()) return;
        juce::SystemClipboard::copyTextToClipboard(query);
        Utils::showAutoCloseAlert(juce::MessageBoxIconType::InfoIcon,
            BM_TJ("live.copy.done") + " : " + query,
            BM_TJ("live.copy.procedure"), 7000);
    }

    Models::Track                     track_;
    SendToDJRouter*                   router_ = nullptr;
    std::unique_ptr<juce::TextButton> sendBtn_;
};

class RediscoverPanel::RowContainer : public juce::Component {
public:
    void setRows(std::vector<std::unique_ptr<Row>> rows) {
        clearRows();
        rows_ = std::move(rows);
        for (auto& r : rows_) addAndMakeVisible(*r);
        resized();
    }

    void clearRows() {
        for (auto& r : rows_) removeChildComponent(r.get());
        rows_.clear();
    }

    int getRowCount() const { return static_cast<int>(rows_.size()); }

    void resized() override {
        int y = 0;
        for (auto& r : rows_) {
            r->setBounds(0, y, getWidth(), kRowHeight);
            y += kRowHeight + kRowSpacing;
        }
        setSize(getWidth(), std::max(1, y));
    }

private:
    std::vector<std::unique_ptr<Row>> rows_;
};

RediscoverPanel::RediscoverPanel() {
    classifier_ = std::make_unique<CamelotMoveClassifier>();

    header_ = std::make_unique<juce::Label>("hdr", juce::String::fromUTF8("\xc3\x80 red\xc3\xa9""couvrir"));
    header_->setFont(bodyFont(16.0f, true));
    header_->setColour(juce::Label::textColourId, Colors::textPrimary());
    addAndMakeVisible(*header_);

    auto mkLabel = [this](const juce::String& txt) {
        auto l = std::make_unique<juce::Label>(txt, txt);
        l->setFont(bodyFont(11.0f));
        l->setColour(juce::Label::textColourId, Colors::textSecondary());
        addAndMakeVisible(*l);
        return l;
    };
    auto styleCb = [this](juce::ComboBox& c) {
        c.setColour(juce::ComboBox::backgroundColourId, Colors::bgCard());
        c.setColour(juce::ComboBox::outlineColourId,    Colors::borderLight());
        c.setColour(juce::ComboBox::textColourId,       Colors::textPrimary());
        c.setColour(juce::ComboBox::arrowColourId,      Colors::textSecondary());
        addAndMakeVisible(c);
    };

    modeLbl_ = mkLabel("Mode");
    modeCb_  = std::make_unique<juce::ComboBox>("mode");
    modeCb_->addItem(juce::String::fromUTF8("Jamais jou\xc3\xa9"),          static_cast<int>(Mode::NeverPlayed));
    modeCb_->addItem(juce::String::fromUTF8("Oubli\xc3\xa9 > 90 j"),         static_cast<int>(Mode::ForgottenOver90d));
    modeCb_->addItem(juce::String::fromUTF8("Moins jou\xc3\xa9"),           static_cast<int>(Mode::LeastPlayed));
    modeCb_->addItem(juce::String::fromUTF8("Moins jou\xc3\xa9 (genre)"), static_cast<int>(Mode::LeastPlayedGenre));
    modeCb_->setSelectedId(static_cast<int>(Mode::NeverPlayed), juce::dontSendNotification);
    modeCb_->onChange = [this] { refresh(); };
    styleCb(*modeCb_);

    genreLbl_ = mkLabel("Genre");
    genreCb_  = std::make_unique<juce::ComboBox>("genre");
    genreCb_->addItem("Tous", 1);
    genreCb_->setSelectedId(1, juce::dontSendNotification);
    genreCb_->onChange = [this] { refresh(); };
    styleCb(*genreCb_);

    compatToggle_ = std::make_unique<juce::ToggleButton>(juce::String::fromUTF8("Compatible avec la piste en cours"));
    compatToggle_->setColour(juce::ToggleButton::textColourId, Colors::textSecondary());
    compatToggle_->setColour(juce::ToggleButton::tickColourId, Colors::success());
    compatToggle_->setColour(juce::ToggleButton::tickDisabledColourId, Colors::borderLight());
    compatToggle_->onClick = [this] { rebuildRows(); };
    addAndMakeVisible(*compatToggle_);

    refreshBtn_ = std::make_unique<juce::TextButton>(juce::String::fromUTF8("Rafra\xc3\xae""chir"));
    refreshBtn_->setColour(juce::TextButton::buttonColourId, Colors::bgLightest());
    refreshBtn_->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    refreshBtn_->onClick = [this] { refresh(); };
    addAndMakeVisible(*refreshBtn_);

    container_ = std::make_unique<RowContainer>();
    viewport_  = std::make_unique<juce::Viewport>();
    viewport_->setViewedComponent(container_.get(), false);
    viewport_->setScrollBarsShown(true, false);
    addAndMakeVisible(*viewport_);

    startTimer(kAutoRefreshMs);
}

RediscoverPanel::~RediscoverPanel() {
    stopTimer();
}

void RediscoverPanel::setDatabase(TrackDatabase* db) {
    db_ = db;
    populateGenreCombo();
    refresh();
}

void RediscoverPanel::setRouter(SendToDJRouter* router) {
    router_ = router;
    rebuildRows();
}

void RediscoverPanel::setSmartEngine(SmartSuggestEngine* engine) {
    smartEngine_ = engine;
    rebuildRows();
}

void RediscoverPanel::onCurrentTrackChanged() {
    rebuildRows();
}

void RediscoverPanel::timerCallback() {
    refresh();
}

RediscoverPanel::Mode RediscoverPanel::currentMode() const {
    if (!modeCb_) return Mode::NeverPlayed;
    const int id = modeCb_->getSelectedId();
    switch (id) {
        case static_cast<int>(Mode::ForgottenOver90d): return Mode::ForgottenOver90d;
        case static_cast<int>(Mode::LeastPlayed):      return Mode::LeastPlayed;
        case static_cast<int>(Mode::LeastPlayedGenre): return Mode::LeastPlayedGenre;
        default:                                       return Mode::NeverPlayed;
    }
}

juce::String RediscoverPanel::currentGenre() const {
    if (!genreCb_) return {};
    if (genreCb_->getSelectedId() <= 1) return {};
    return genreCb_->getText();
}

void RediscoverPanel::populateGenreCombo() {
    if (!genreCb_ || !db_) return;

    std::vector<Models::Track> genreRows;
    try {
        genreRows = db_->getTracksByQuery(
            "SELECT * FROM tracks WHERE genre IS NOT NULL AND genre != '' "
            "GROUP BY genre ORDER BY genre ASC LIMIT 500");
    } catch (...) {
        return;
    }

    const juce::String prev = genreCb_->getText();
    genreCb_->clear(juce::dontSendNotification);
    genreCb_->addItem("Tous", 1);

    std::set<std::string> seen;
    int nextId = 2;
    for (const auto& t : genreRows) {
        if (t.genre.empty()) continue;
        if (!seen.insert(t.genre).second) continue;
        genreCb_->addItem(juce::String(t.genre), nextId++);
    }

    int foundId = 1;
    for (int i = 0; i < genreCb_->getNumItems(); ++i)
        if (genreCb_->getItemText(i) == prev) {
            foundId = genreCb_->getItemId(i);
            break;
        }
    genreCb_->setSelectedId(foundId, juce::dontSendNotification);
}

std::vector<RediscoverPanel::Entry> RediscoverPanel::fetchEntries() const {
    std::vector<Entry> out;
    if (!db_) return out;

    const Mode mode = currentMode();
    const juce::String genre = currentGenre();

    std::vector<Models::Track> tracks;
    try {
        switch (mode) {
            case Mode::NeverPlayed: {
                if (genre.isNotEmpty()) {
                    tracks = db_->getTracksByQuery(
                        "SELECT * FROM tracks "
                        "WHERE (play_count = 0 OR play_count IS NULL) "
                        "AND genre = ? "
                        "ORDER BY date_added DESC LIMIT 200",
                        { genre.toStdString() });
                } else {
                    tracks = db_->getTracksByQuery(
                        "SELECT * FROM tracks "
                        "WHERE play_count = 0 OR play_count IS NULL "
                        "ORDER BY date_added DESC LIMIT 200");
                }
                break;
            }
            case Mode::ForgottenOver90d: {
                if (genre.isNotEmpty()) {
                    tracks = db_->getTracksByQuery(
                        "SELECT * FROM tracks "
                        "WHERE last_played < strftime('%s','now','-90 days') "
                        "AND play_count > 0 AND genre = ? "
                        "ORDER BY last_played ASC LIMIT 200",
                        { genre.toStdString() });
                } else {
                    tracks = db_->getTracksByQuery(
                        "SELECT * FROM tracks "
                        "WHERE last_played < strftime('%s','now','-90 days') "
                        "AND play_count > 0 "
                        "ORDER BY last_played ASC LIMIT 200");
                }
                break;
            }
            case Mode::LeastPlayed: {
                tracks = db_->getTracksByQuery(
                    "SELECT * FROM tracks WHERE play_count > 0 "
                    "ORDER BY play_count ASC, last_played ASC LIMIT 200");
                break;
            }
            case Mode::LeastPlayedGenre: {
                if (genre.isEmpty()) {
                    tracks = db_->getTracksByQuery(
                        "SELECT * FROM tracks WHERE play_count > 0 "
                        "ORDER BY play_count ASC, last_played ASC LIMIT 200");
                } else {
                    tracks = db_->getTracksByQuery(
                        "SELECT * FROM tracks "
                        "WHERE play_count > 0 AND genre = ? "
                        "ORDER BY play_count ASC, last_played ASC LIMIT 200",
                        { genre.toStdString() });
                }
                break;
            }
        }
    } catch (...) {
        return out;
    }

    out.reserve(tracks.size());
    for (auto& t : tracks) {
        Entry e;
        e.track = std::move(t);
        out.push_back(std::move(e));
    }
    return out;
}

bool RediscoverPanel::passesCompatibilityFilter(const Models::Track& t) const {
    if (!compatToggle_ || !compatToggle_->getToggleState()) return true;
    if (!smartEngine_ || !classifier_ || !db_) return true;

    const int64_t currentId = smartEngine_->getCurrentTrack();
    if (currentId <= 0) return true;

    Models::Track current;
    try {
        auto opt = db_->getTrack(currentId);
        if (!opt.has_value()) return true;
        current = *opt;
    } catch (...) {
        return true;
    }

    const std::string from = !current.camelotKey.empty() ? current.camelotKey : current.key;
    const std::string to   = !t.camelotKey.empty()       ? t.camelotKey       : t.key;
    if (from.empty() || to.empty()) return false;

    const auto res = classifier_->classify(from, to);
    return isCompatibleMove(res.move);
}

void RediscoverPanel::refresh() {
    entries_ = fetchEntries();
    rebuildRows();
    repaint();
}

void RediscoverPanel::rebuildRows() {
    if (!container_) return;
    container_->clearRows();
    if (entries_.empty()) {
        container_->setSize(viewport_ ? viewport_->getWidth() : getWidth(), 1);
        return;
    }

    std::vector<std::unique_ptr<Row>> rows;
    rows.reserve(entries_.size());
    for (const auto& e : entries_) {
        if (!passesCompatibilityFilter(e.track)) continue;
        rows.push_back(std::make_unique<Row>(e.track, router_));
    }

    const int w = viewport_ ? viewport_->getWidth() : getWidth();
    container_->setSize(std::max(1, w), 1);
    container_->setRows(std::move(rows));
}

void RediscoverPanel::paint(juce::Graphics& g) {
    g.fillAll(Colors::bgSurface());

    if (container_ && container_->getRowCount() == 0) {
        auto r = getLocalBounds().reduced(12);
        r.removeFromTop(120);
        g.setColour(Colors::textMuted());
        g.setFont(bodyFont(13.0f, false));
        g.drawFittedText(
            juce::String::fromUTF8("Aucun titre \xc3\xa0 red\xc3\xa9""couvrir pour ce filtre \xe2\x80\x94 essayez un autre mode"),
            r, juce::Justification::centred, 3);
    }
}

void RediscoverPanel::resized() {
    auto r = getLocalBounds().reduced(8);

    auto headerRow = r.removeFromTop(28);
    if (header_)     header_->setBounds(headerRow.removeFromLeft(220));
    if (refreshBtn_) refreshBtn_->setBounds(headerRow.removeFromRight(110));

    r.removeFromTop(6);

    auto row1 = r.removeFromTop(46);
    {
        auto col = row1.removeFromLeft(row1.getWidth() / 2).reduced(0, 2);
        auto lbl = col.removeFromTop(14);
        if (modeLbl_) modeLbl_->setBounds(lbl);
        if (modeCb_)  modeCb_->setBounds(col.reduced(0, 2));
    }
    {
        auto col = row1.reduced(4, 2);
        auto lbl = col.removeFromTop(14);
        if (genreLbl_) genreLbl_->setBounds(lbl);
        if (genreCb_)  genreCb_->setBounds(col.reduced(0, 2));
    }

    r.removeFromTop(2);
    auto row2 = r.removeFromTop(26);
    if (compatToggle_) compatToggle_->setBounds(row2);

    r.removeFromTop(6);
    if (viewport_) {
        viewport_->setBounds(r);
        if (container_)
            container_->setSize(r.getWidth(), container_->getHeight());
    }
}

}
