#include "SuggestionPanel.h"

#include "../../styles/ColorPalette.h"
#include "../../utils/AutoCloseAlert.h"
#include "../../../app/Application.h"
#include "../../../app/ServiceLocator.h"
#include "../../../services/config/I18n.h"
#include "../../../services/library/TrackDatabase.h"
#include "../../../services/ai/ClapEmbedQueue.h"
#include "../../../services/suggestions/DJProfileService.h"
#include "../../../models/DJProfile.h"
#include "../../utils/ViewPrefs.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace BeatMate::UI::Widgets {

using Services::Suggestions::Suggestion;
using Services::Suggestions::SmartSuggestEngine;
using Services::Suggestions::EnergyDirection;

namespace {

juce::Colour levelColour(Suggestion::Level l) {
    switch (l) {
        case Suggestion::Level::Green:  return Colors::success();
        case Suggestion::Level::Yellow: return Colors::warning();
        case Suggestion::Level::Red:    return Colors::error();
    }
    return Colors::textMuted();
}

void copyQueryToClipboard(const juce::String& artist, const juce::String& title) {
    const auto query = (artist + " " + title).trim();
    if (query.isEmpty()) return;
    juce::SystemClipboard::copyTextToClipboard(query);
    Utils::showAutoCloseAlert(juce::MessageBoxIconType::InfoIcon,
        BM_TJ("live.copy.done") + " : " + query,
        BM_TJ("live.copy.procedure"), 7000);
}

} // namespace

class SuggestionPanel::SuggestionCard : public juce::Component,
                                        public juce::SettableTooltipClient {
public:
    using StarFn    = std::function<bool(int64_t)>;
    using SkipFn    = std::function<void(int64_t)>;
    using AssocFn   = std::function<bool(int64_t)>;
    using ExploreFn = std::function<void(const Suggestion&)>;

    struct Ctx {
        juce::String relationName;
        double       curBpm    = 0.0;
        double       curEnergy = 0.0;
        juce::String explain;
    };

    SuggestionCard(const Suggestion& s,
                   const Ctx& ctx,
                   bool favorited,
                   StarFn onStar,
                   SkipFn onSkip,
                   bool associated,
                   AssocFn onAssoc,
                   ExploreFn onExplore)
        : data_(s), ctx_(ctx),
          favorited_(favorited), associated_(associated),
          onStar_(std::move(onStar)), onSkip_(std::move(onSkip)),
          onAssoc_(std::move(onAssoc)), onExplore_(std::move(onExplore))
    {
        sendBtn_ = std::make_unique<juce::TextButton>(BM_TJ("live.sendToDj"));
        sendBtn_->setTooltip(juce::String::fromUTF8(
            "Copie \xc2\xab Artiste Titre \xc2\xbb \xe2\x80\x94 collez avec Ctrl+F dans votre logiciel DJ."));
        sendBtn_->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
        sendBtn_->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        sendBtn_->onClick = [this] { copyToClipboard(); };
        addAndMakeVisible(*sendBtn_);

        exploreBtn_ = std::make_unique<juce::TextButton>(
            juce::CharPointer_UTF8("\xC2\xBB"));
        exploreBtn_->setTooltip(juce::String::fromUTF8(
            "Partir de ce titre : explorer les encha\xc3\xaenements suivants (2 coups d'avance) sans toucher au live."));
        exploreBtn_->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
        exploreBtn_->setColour(juce::TextButton::textColourOffId, Colors::primaryHover());
        exploreBtn_->onClick = [this] { if (onExplore_) onExplore_(data_); };
        addAndMakeVisible(*exploreBtn_);

        starBtn_ = std::make_unique<juce::TextButton>(
            juce::CharPointer_UTF8("\xE2\x98\x85"));
        starBtn_->setTooltip(BM_TJ("suggest.tip.favorite"));
        starBtn_->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
        starBtn_->setColour(juce::TextButton::textColourOffId,
            favorited_ ? Colors::starFilled() : Colors::textPrimary());
        starBtn_->onClick = [this] {
            if (!onStar_) return;
            favorited_ = onStar_(data_.trackId);
            starBtn_->setColour(juce::TextButton::textColourOffId,
                favorited_ ? Colors::starFilled() : Colors::textPrimary());
            repaint();
        };
        addAndMakeVisible(*starBtn_);

        skipBtn_ = std::make_unique<juce::TextButton>(
            juce::CharPointer_UTF8("\xE2\x86\x92"));
        skipBtn_->setTooltip(BM_TJ("suggest.tip.skip"));
        skipBtn_->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
        skipBtn_->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        skipBtn_->onClick = [this] {
            if (onSkip_) onSkip_(data_.trackId);
        };
        addAndMakeVisible(*skipBtn_);

        assocBtn_ = std::make_unique<juce::TextButton>(
            juce::CharPointer_UTF8("\xE2\x86\xA9"));
        assocBtn_->setTooltip(juce::String::fromUTF8(
            "Ces deux-l\xc3\xa0 marchent : m\xc3\xa9morise la paire (piste courante \xe2\x86\x92 ce titre) pour les prochaines suggestions."));
        assocBtn_->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
        assocBtn_->setColour(juce::TextButton::textColourOffId,
            associated_ ? Colors::success() : Colors::textPrimary());
        assocBtn_->onClick = [this] {
            if (!onAssoc_) return;
            associated_ = onAssoc_(data_.trackId);
            assocBtn_->setColour(juce::TextButton::textColourOffId,
                associated_ ? Colors::success() : Colors::textPrimary());
            repaint();
        };
        addAndMakeVisible(*assocBtn_);

        if (ctx_.explain.isNotEmpty())
            setTooltip(ctx_.explain);
        else
            setTooltip(juce::String::fromUTF8(
                "Double-clic : copier \xc2\xab Artiste Titre \xc2\xbb pour votre logiciel DJ"));
    }

    void copyToClipboard() {
        copyQueryToClipboard(juce::String::fromUTF8(data_.artist.c_str()),
                             juce::String::fromUTF8(data_.title.c_str()));
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isRightButtonDown())
            copyToClipboard();
    }

    void mouseDoubleClick(const juce::MouseEvent&) override {
        copyToClipboard();
    }

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(2.0f);
        const auto lvl = levelColour(data_.level);

        g.setColour(Colors::bgCard());
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(Colors::glassWhite());
        g.fillRoundedRectangle(r.getX(), r.getY(), r.getWidth(), 1.5f, 8.0f);
        if (favorited_) {
            g.setColour(Colors::starFilled());
            g.drawRoundedRectangle(r, 8.0f, 2.0f);
        } else {
            g.setColour(Colors::glassBorder());
            g.drawRoundedRectangle(r, 8.0f, 1.0f);
        }
        g.setColour(lvl.withAlpha(0.9f));
        g.fillRoundedRectangle(r.getX() + 1.0f, r.getY() + 6.0f, 3.0f, r.getHeight() - 12.0f, 1.5f);

        auto scoreCol = r.removeFromLeft(52.0f);
        auto badge = scoreCol.reduced(6.0f, 0.0f).withSizeKeepingCentre(40.0f, 34.0f);
        g.setColour(lvl.withAlpha(0.16f));
        g.fillRoundedRectangle(badge, 8.0f);
        g.setColour(lvl);
        g.drawRoundedRectangle(badge, 8.0f, 1.2f);
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        const int matchPct = juce::jlimit(0, 100, (int) data_.totalScore);
        g.drawFittedText(juce::String(matchPct),
                         badge.toNearestInt().withTrimmedBottom(10),
                         juce::Justification::centredBottom, 1);
        g.setFont(juce::Font(8.0f, juce::Font::bold));
        g.drawFittedText("/100", badge.toNearestInt().withTrimmedTop(20),
                         juce::Justification::centredTop, 1);
        if (data_.isWildCard) {
            g.setColour(Colors::secondary());
            g.setFont(juce::Font(8.0f, juce::Font::bold));
            g.drawFittedText("WILD", scoreCol.toNearestInt().withTrimmedTop(62),
                             juce::Justification::centredTop, 1);
        }

        auto text = r.reduced(6.0f, 2.0f);
        text.removeFromBottom(22.0f);

        auto line = text.removeFromTop(16.0f);
        g.setColour(Colors::textPrimary());
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.drawFittedText(juce::String::fromUTF8(data_.title.c_str())
                             + juce::String::fromUTF8("   \xe2\x80\x94   ")
                             + juce::String::fromUTF8(data_.artist.c_str()),
                         line.toNearestInt(), juce::Justification::centredLeft, 1);

        auto badges = text.removeFromTop(18.0f).reduced(0.0f, 1.0f);
        auto drawPill = [&g, &badges](const juce::String& txt, juce::Colour col) {
            juce::Font f(10.5f, juce::Font::bold);
            const float w = juce::GlyphArrangement::getStringWidth(f, txt) + 16.0f;
            if (badges.getWidth() < w) return;
            auto pill = badges.removeFromLeft(w);
            badges.removeFromLeft(5.0f);
            g.setColour(col.withAlpha(0.14f));
            g.fillRoundedRectangle(pill, pill.getHeight() * 0.5f);
            g.setColour(col.withAlpha(0.45f));
            g.drawRoundedRectangle(pill, pill.getHeight() * 0.5f, 0.8f);
            g.setColour(col);
            g.setFont(f);
            g.drawText(txt, pill.toNearestInt(), juce::Justification::centred);
        };

        if (data_.bpm > 0.0) {
            juce::String bpmTxt = juce::String(data_.bpm, 1) + " BPM";
            if (ctx_.curBpm > 0.0 || std::abs(data_.bpmDelta) > 0.001) {
                const double d = data_.bpmDelta;
                bpmTxt << "  " << (d >= 0.0 ? "+" : "") << juce::String(d, 1);
            }
            drawPill(bpmTxt, Colors::bpmBadge());
        }
        if (!data_.camelotKey.empty()) {
            juce::String keyTxt = juce::String::fromUTF8(data_.camelotKey.c_str());
            if (ctx_.relationName.isNotEmpty())
                keyTxt << juce::String::fromUTF8(" \xc2\xb7 ") << ctx_.relationName;
            drawPill(keyTxt, Colors::keyBadge());
        }
        if (data_.energyScore > 0.0) {
            juce::String eTxt;
            if (ctx_.curEnergy > 0.0)
                eTxt = juce::String((int) std::lround(ctx_.curEnergy))
                     + juce::String::fromUTF8("\xe2\x86\x92")
                     + juce::String((int) std::lround(data_.energyScore));
            else
                eTxt = juce::String::fromUTF8("\xE2\x9A\xA1 ")
                     + juce::String((int) std::lround(data_.energyScore)) + "/10";
            drawPill(eTxt, Colors::energyBadge());
        }
        if (data_.timbreSim > 0.0f && data_.timbreScore > 0)
            drawPill("IA " + juce::String(data_.timbreScore) + " %", Colors::secondary());
        if (!data_.genre.empty())
            drawPill(juce::String::fromUTF8(data_.genre.c_str()).trim(), Colors::textSecondary());
        if (data_.manualPair)
            drawPill(juce::String::fromUTF8("\xF0\x9F\x94\x97 associ\xc3\xa9s"), Colors::success());

        if (!data_.reason.empty()) {
            line = text.removeFromTop(13.0f);
            g.setColour(Colors::textMuted());
            g.setFont(juce::Font(10.0f, juce::Font::italic));
            g.drawFittedText(juce::String::fromUTF8(data_.reason.c_str()),
                             line.toNearestInt(), juce::Justification::centredLeft, 1);
        }

        if (!data_.transitionLabel.empty() && text.getHeight() >= 15.0f) {
            line = text.removeFromTop(17.0f);
            juce::String tlabel = juce::String::fromUTF8("\xe2\x9c\x93 ")
                + juce::String::fromUTF8(data_.transitionLabel.c_str());
            if (data_.mixBars > 0)
                tlabel << juce::String::fromUTF8("  \xc2\xb7  ")
                       << juce::String(data_.mixBars)
                       << juce::String::fromUTF8(" mesures");
            juce::Font chipFont(10.5f, juce::Font::bold);
            g.setFont(chipFont);
            const int chipW = juce::jmin((int) line.getWidth(),
                (int) juce::GlyphArrangement::getStringWidth(chipFont, tlabel) + 18);
            auto chip = line.removeFromLeft((float) chipW).withTrimmedBottom(3.0f);
            g.setColour(Colors::primary().withAlpha(0.16f));
            g.fillRoundedRectangle(chip, chip.getHeight() * 0.5f);
            g.setColour(Colors::primary().withAlpha(0.4f));
            g.drawRoundedRectangle(chip, chip.getHeight() * 0.5f, 0.8f);
            g.setColour(Colors::primaryHover());
            g.drawText(tlabel, chip.toNearestInt(), juce::Justification::centred);
        }
    }

    void resized() override {
        auto r = getLocalBounds();
        r.removeFromLeft(52);
        auto btnRow = r.removeFromBottom(22).reduced(6, 1);
        skipBtn_->setBounds(btnRow.removeFromRight(26));
        btnRow.removeFromRight(2);
        assocBtn_->setBounds(btnRow.removeFromRight(26));
        btnRow.removeFromRight(2);
        starBtn_->setBounds(btnRow.removeFromRight(26));
        btnRow.removeFromRight(2);
        exploreBtn_->setBounds(btnRow.removeFromRight(28));
        btnRow.removeFromRight(4);
        sendBtn_->setBounds(btnRow.removeFromRight(juce::jmin(105, btnRow.getWidth())));
    }

private:
    Suggestion data_;
    Ctx ctx_;
    bool favorited_ = false;
    bool associated_ = false;
    StarFn onStar_;
    SkipFn onSkip_;
    AssocFn onAssoc_;
    ExploreFn onExplore_;
    std::unique_ptr<juce::TextButton> sendBtn_;
    std::unique_ptr<juce::TextButton> exploreBtn_;
    std::unique_ptr<juce::TextButton> starBtn_;
    std::unique_ptr<juce::TextButton> skipBtn_;
    std::unique_ptr<juce::TextButton> assocBtn_;
};

class SuggestionPanel::CardContainer : public juce::Component {
public:
    void setCards(std::vector<std::unique_ptr<SuggestionCard>> cards) {
        cards_ = std::move(cards);
        for (auto& c : cards_) addAndMakeVisible(*c);
        resized();
    }

    void clearCards() {
        for (auto& c : cards_) removeChildComponent(c.get());
        cards_.clear();
    }

    int getCardCount() const { return static_cast<int>(cards_.size()); }

    void resized() override {
        const int cardH = 88;
        const int spacing = 3;
        int y = 0;
        for (auto& c : cards_) {
            c->setBounds(0, y, getWidth(), cardH);
            y += cardH + spacing;
        }
        setSize(getWidth(), std::max(1, y));
    }

private:
    std::vector<std::unique_ptr<SuggestionCard>> cards_;
};

SuggestionPanel::SuggestionPanel() {
    header_ = std::make_unique<juce::Label>("hdr", BM_TJ("suggest.title"));
    header_->setFont(juce::Font(16.0f, juce::Font::bold));
    header_->setColour(juce::Label::textColourId, Colors::textPrimary());
    addAndMakeVisible(*header_);

    exploreLbl_ = std::make_unique<juce::Label>("exploreLbl", juce::String());
    exploreLbl_->setFont(juce::Font(12.0f, juce::Font::bold));
    exploreLbl_->setColour(juce::Label::textColourId, Colors::textPrimary());
    exploreLbl_->setColour(juce::Label::backgroundColourId,
                           Colors::secondary().withAlpha(0.18f));
    exploreLbl_->setBorderSize(juce::BorderSize<int>(2, 8, 2, 8));
    addChildComponent(*exploreLbl_);

    exploreBackBtn_ = std::make_unique<juce::TextButton>(
        juce::String::fromUTF8("Revenir au live"));
    exploreBackBtn_->setTooltip(juce::String::fromUTF8(
        "Quitte l'exploration et revient aux suggestions de la piste r\xc3\xa9""ellement en cours."));
    exploreBackBtn_->setColour(juce::TextButton::buttonColourId, Colors::secondary());
    exploreBackBtn_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    exploreBackBtn_->onClick = [this] { exitExploration(); };
    addChildComponent(*exploreBackBtn_);

    refreshBtn_ = std::make_unique<juce::TextButton>(BM_TJ("live.refresh"));
    refreshBtn_->setTooltip(juce::String::fromUTF8(
        "Recalcule les suggestions pour la piste courante."));
    refreshBtn_->setColour(juce::TextButton::buttonColourId, Colors::bgLightest());
    refreshBtn_->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    refreshBtn_->setColour(juce::TextButton::textColourOnId,  Colors::textPrimary());
    refreshBtn_->onClick = [this] { refresh(); };
    addAndMakeVisible(*refreshBtn_);

    wheel_ = std::make_unique<CamelotWheelWidget>();
    wheel_->onKeyClicked = [this](const std::string& key) {
        if (!engine_) return;
        if (key == wheelFocusKey_) {
            wheelFocusKey_.clear();
            engine_->setHarmonicOnly(false);
            engine_->setKeyOverride("");
            if (wheel_) wheel_->setCurrentKey(curKey_);
        } else {
            wheelFocusKey_ = key;
            engine_->setHarmonicOnly(true);
            engine_->setKeyOverride(key);
            if (wheel_) wheel_->setCurrentKey(key);
        }
        refresh();
    };
    addAndMakeVisible(*wheel_);

    container_ = std::make_unique<CardContainer>();
    viewport_ = std::make_unique<juce::Viewport>();
    viewport_->setViewedComponent(container_.get(), false);
    viewport_->setScrollBarsShown(true, false);
    addAndMakeVisible(*viewport_);
}

SuggestionPanel::~SuggestionPanel() = default;

void SuggestionPanel::setCurrentCamelotKey(const std::string& camelot) {
    if (wheel_) wheel_->setCurrentKey(camelot);
}

void SuggestionPanel::setEngine(SmartSuggestEngine* engine) {
    engine_ = engine;
    refresh();
}

void SuggestionPanel::setPlaceholderVisible(bool visible) {
    placeholder_ = visible;
    if (visible) {
        results_.clear();
        container_->clearCards();
    }
    repaint();
}

void SuggestionPanel::refresh() {
    if (!engine_) {
        setPlaceholderVisible(true);
        return;
    }
    if (engine_->getCurrentTrack() <= 0) {
        setPlaceholderVisible(true);
        return;
    }

    if (exploring_ && engine_->getCurrentTrack() != explorationId_) {
        exploring_ = false;
        if (exploreLbl_)     exploreLbl_->setVisible(false);
        if (exploreBackBtn_) exploreBackBtn_->setVisible(false);
        resized();
    }

    const uint64_t myGen = refreshGen_.fetch_add(1) + 1;
    auto* engine = engine_;
    juce::Component::SafePointer<SuggestionPanel> safe(this);

    struct JobOut {
        std::vector<Suggestion> results;
        std::unordered_map<int64_t, juce::String> explains;
        std::string curKey;
        double curBpm = 0.0;
        double curEnergy = 0.0;
    };
    auto job = [safe, engine, myGen]() {
        auto out = std::make_shared<JobOut>();
        try {
            out->results = engine->getSuggestions(150);
        } catch (...) {}
        try {
            extern BeatMate::ServiceLocator* g_serviceLocator;
            if (g_serviceLocator) {
                if (auto* clap = g_serviceLocator->tryGet<Services::AI::ClapEmbedQueue>()) {
                    std::vector<int64_t> ids;
                    ids.reserve(out->results.size() + 1);
                    ids.push_back(engine->getCurrentTrack());
                    for (const auto& s : out->results) ids.push_back(s.trackId);
                    clap->prioritizeTracks(ids);
                }
            }
        } catch (...) {}
        try {
            extern BeatMate::ServiceLocator* g_serviceLocator;
            if (g_serviceLocator) {
                if (auto* db = g_serviceLocator->tryGet<Services::Library::TrackDatabase>()) {
                    if (auto cur = db->getTrack(engine->getCurrentTrack());
                        cur.has_value() && cur->id > 0) {
                        out->curKey    = !cur->camelotKey.empty() ? cur->camelotKey : cur->key;
                        out->curBpm    = cur->bpm;
                        out->curEnergy = cur->energy;
                    }
                }
            }
        } catch (...) {}
        juce::MessageManager::callAsync([safe, out, myGen]() {
            auto* self = safe.getComponent();
            if (!self) return;
            if (myGen != self->refreshGen_.load()) return;
            self->results_     = std::move(out->results);
            self->explainById_ = std::move(out->explains);
            self->curKey_      = out->curKey;
            self->curBpm_      = out->curBpm;
            self->curEnergy_   = out->curEnergy;
            if (self->wheel_)
                self->wheel_->setCurrentKey(self->wheelFocusKey_.empty()
                                                ? self->curKey_ : self->wheelFocusKey_);
            self->placeholder_ = self->results_.empty();
            self->rebuildCards();
            self->repaint();
        });
    };

    if (auto* pool = ::BeatMate::getBackgroundPool()) {
        pool->addJob(job);
    } else {
        job();
    }
}

void SuggestionPanel::rebuildCards() {
    container_->clearCards();
    if (header_)
        header_->setText(BM_TJ("suggest.title")
                             + juce::String::fromUTF8("  \xc2\xb7  ")
                             + juce::String((int) results_.size()),
                         juce::dontSendNotification);
    if (results_.empty()) {
        container_->setSize(viewport_->getWidth(), 1);
        return;
    }

    auto starCb = [this](int64_t id) -> bool { return toggleFavorite(id); };
    auto skipCb = [this](int64_t id)         { skipTrack(id); };
    auto assocCb = [this](int64_t id) -> bool { return toggleAssociation(id); };
    auto exploreCb = [this](const Suggestion& s) { startExploration(s); };
    auto makeCard = [&](const Suggestion& s) {
        SuggestionCard::Ctx ctx;
        ctx.curBpm    = curBpm_;
        ctx.curEnergy = curEnergy_;
        if (!curKey_.empty() && !s.camelotKey.empty())
            ctx.relationName = juce::String::fromUTF8(
                SmartSuggestEngine::harmonicRelationName(
                    curKey_, s.camelotKey, engine_ && engine_->getEnergyBoost()).c_str());
        if (auto it = explainById_.find(s.trackId); it != explainById_.end())
            ctx.explain = it->second;
        return std::make_unique<SuggestionCard>(
            s, ctx, isFavorite(s.trackId), starCb, skipCb,
            isAssociated(s.trackId), assocCb, exploreCb);
    };

    auto pool = results_;
    std::stable_sort(pool.begin(), pool.end(),
        [](const Suggestion& a, const Suggestion& b) {
            return a.totalScore > b.totalScore;
        });

    std::vector<std::unique_ptr<SuggestionCard>> cards;
    cards.reserve(pool.size());
    for (const auto& s : pool)
        cards.push_back(makeCard(s));

    container_->setSize(viewport_->getWidth(), 1);
    container_->setCards(std::move(cards));
}

void SuggestionPanel::startExploration(const Suggestion& s) {
    if (!engine_ || s.trackId <= 0) return;
    if (!exploring_)
        liveTrackId_ = engine_->getCurrentTrack();
    exploring_        = true;
    explorationId_    = s.trackId;
    explorationTitle_ = (juce::String::fromUTF8(s.artist.c_str()) + " "
                         + juce::String::fromUTF8("\xe2\x80\x93") + " "
                         + juce::String::fromUTF8(s.title.c_str())).trim();
    engine_->setCurrentTrack(s.trackId);
    engine_->invalidateCache();
    if (exploreLbl_) {
        exploreLbl_->setText(juce::String::fromUTF8("Exploration depuis : ")
                                 + explorationTitle_,
                             juce::dontSendNotification);
        exploreLbl_->setVisible(true);
    }
    if (exploreBackBtn_) exploreBackBtn_->setVisible(true);
    resized();
    refresh();
}

void SuggestionPanel::exitExploration() {
    if (!exploring_) return;
    exploring_ = false;
    if (exploreLbl_)     exploreLbl_->setVisible(false);
    if (exploreBackBtn_) exploreBackBtn_->setVisible(false);
    if (engine_ && liveTrackId_ > 0) {
        engine_->setCurrentTrack(liveTrackId_);
        engine_->invalidateCache();
    }
    resized();
    refresh();
}

void SuggestionPanel::paint(juce::Graphics& g) {
    g.fillAll(Colors::bgSurface());

    if (placeholder_) {
        auto r = getLocalBounds().reduced(12);
        r.removeFromTop(90); // below header
        g.setColour(Colors::textMuted());
        g.setFont(juce::Font(13.0f, juce::Font::italic));
        g.drawFittedText(
            BM_TJ("suggest.placeholder.noCurrent"),
            r, juce::Justification::centred, 3);
        return;
    }

    if (container_ && container_->getCardCount() == 0 && !results_.empty()) {
        auto r = getLocalBounds().reduced(12);
        r.removeFromTop(120);
        g.setColour(Colors::warning());
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawFittedText(
            BM_TJ("suggest.placeholder.lowScores.title"),
            r.removeFromTop(26), juce::Justification::centred, 1);
        g.setColour(Colors::textSecondary());
        g.setFont(juce::Font(12.0f));
        g.drawFittedText(
            BM_TJ("suggest.placeholder.lowScores.hint"),
            r.removeFromTop(80), juce::Justification::centredTop, 4);
    }
}

void SuggestionPanel::resized() {
    auto r = getLocalBounds().reduced(8);

    auto headerRow = r.removeFromTop(28);
    refreshBtn_->setBounds(headerRow.removeFromRight(80));
    headerRow.removeFromRight(6);
    header_->setBounds(headerRow);

    juce::Rectangle<int> wheelArea;
    if (wheel_) {
        auto rightCol = r.removeFromRight(210);
        rightCol.removeFromLeft(10);
        wheelArea = rightCol.removeFromTop(200);
        wheel_->setBounds(wheelArea);
    }

    r.removeFromTop(4);

    if (exploring_ && exploreLbl_ && exploreBackBtn_) {
        auto banner = r.removeFromTop(26);
        exploreBackBtn_->setBounds(banner.removeFromRight(120).reduced(0, 1));
        banner.removeFromRight(6);
        exploreLbl_->setBounds(banner);
        r.removeFromTop(4);
    }

    r.removeFromTop(8);
    viewport_->setBounds(r);
    container_->setSize(r.getWidth(), container_->getHeight());
}

void SuggestionPanel::setProfileContext(
    Services::Suggestions::DJProfileService* svc, std::string activeProfileName)
{
    profileService_   = svc;
    activeProfileName_ = std::move(activeProfileName);
    if (engine_ && profileService_ && !activeProfileName_.empty()) {
        if (auto p = profileService_->loadProfile(activeProfileName_))
            engine_->boostIds(p->favorites);
    }
    reloadAssociationsFromProfile();
    rebuildCards();
}

void SuggestionPanel::reloadAssociationsFromProfile() {
    if (!engine_) return;
    if (!profileService_ || activeProfileName_.empty()) {
        return;
    }
    auto p = profileService_->loadProfile(activeProfileName_);
    if (!p) return;
    std::vector<std::pair<int64_t, int64_t>> assoc;
    assoc.reserve(p->associations.size());
    for (const auto& a : p->associations)
        if (a[0] > 0 && a[1] > 0) assoc.emplace_back(a[0], a[1]);
    engine_->setManualAssociations(assoc);
}

bool SuggestionPanel::isAssociated(int64_t trackId) const {
    if (!engine_) return false;
    const int64_t cur = engine_->getCurrentTrack();
    if (cur <= 0) return false;
    return engine_->isAssociated(cur, trackId);
}

bool SuggestionPanel::toggleAssociation(int64_t trackId) {
    if (!engine_) return false;
    const int64_t cur = engine_->getCurrentTrack();
    if (cur <= 0 || trackId <= 0 || cur == trackId) return false;

    const bool wasAssoc = engine_->isAssociated(cur, trackId);
    bool nowAssoc;
    if (wasAssoc) {
        engine_->unassociateTracks(cur, trackId);
        nowAssoc = false;
    } else {
        engine_->associateTracks(cur, trackId, 3);
        engine_->reportAccepted(trackId);
        nowAssoc = true;
    }

    if (profileService_ && !activeProfileName_.empty()) {
        if (auto p = profileService_->loadProfile(activeProfileName_)) {
            auto& v = p->associations;
            auto it = std::find_if(v.begin(), v.end(),
                [cur, trackId](const std::array<int64_t, 2>& a) {
                    return a[0] == cur && a[1] == trackId;
                });
            if (nowAssoc) {
                if (it == v.end()) v.push_back({ cur, trackId });
            } else if (it != v.end()) {
                v.erase(it);
            }
            profileService_->saveProfile(*p);
        }
    }
    refresh();
    return nowAssoc;
}

bool SuggestionPanel::isFavorite(int64_t trackId) const {
    if (!profileService_ || activeProfileName_.empty()) return false;
    auto p = const_cast<Services::Suggestions::DJProfileService*>(profileService_)
        ->loadProfile(activeProfileName_);
    if (!p) return false;
    return std::find(p->favorites.begin(), p->favorites.end(), trackId)
        != p->favorites.end();
}

bool SuggestionPanel::toggleFavorite(int64_t trackId) {
    if (!profileService_ || activeProfileName_.empty()) return false;
    auto p = profileService_->loadProfile(activeProfileName_);
    if (!p) return false;
    auto& fav = p->favorites;
    bool nowFav;
    auto it = std::find(fav.begin(), fav.end(), trackId);
    if (it == fav.end()) { fav.push_back(trackId); nowFav = true; }
    else                 { fav.erase(it);          nowFav = false; }
    profileService_->saveProfile(*p);
    if (engine_) engine_->boostIds(fav);
    refresh();
    return nowFav;
}

void SuggestionPanel::skipTrack(int64_t trackId) {
    if (engine_) engine_->blacklistAdd(trackId);
    if (profileService_ && !activeProfileName_.empty()) {
        if (auto p = profileService_->loadProfile(activeProfileName_)) {
            if (std::find(p->skipped.begin(), p->skipped.end(), trackId)
                == p->skipped.end()) {
                p->skipped.push_back(trackId);
                profileService_->saveProfile(*p);
            }
        }
    }
    results_.erase(std::remove_if(results_.begin(), results_.end(),
        [trackId](const Suggestion& s) { return s.trackId == trackId; }),
        results_.end());
    rebuildCards();
    refresh();
}

} // namespace BeatMate::UI::Widgets
