#include "LiveSuggestView.h"
#include "../../services/update/UpdateService.h"
#include "../styles/ColorPalette.h"
#include "../utils/AutoCloseAlert.h"
#include "../utils/ViewPrefs.h"
#include "../../services/library/TrackDataProvider.h"
#include "../../services/library/PlaylistManager.h"
#include "../../services/djsoftware/virtualdj/VirtualDJRemote.h"
#include "../../services/audio/AudioListenerService.h"
#include "../../services/djsoftware/rekordbox/RekordboxService.h"
#include "../../services/live/NowPlayingService.h"
#include "../../services/djsoftware/UnifiedDJHistory.h"
#include "../../services/djsoftware/SendToDJRouter.h"
#include "../../services/djsoftware/rekordbox/RekordboxAnlzParser.h"
#include "../../services/library/TrackDatabase.h"
#include "../../services/library/PeakFileService.h"
#include "../../services/ai/ClapEmbedQueue.h"
#include "../../app/ServiceLocator.h"
#include "../../app/Application.h"
#include "../../services/config/I18n.h"
#include "../../models/Track.h"
#include "../../services/suggestions/SmartSuggestEngine.h"
#include "../../services/suggestions/DJProfileService.h"
#include "../../models/DJProfile.h"
#include "../widgets/suggestions/SuggestionPanel.h"
#include "../widgets/suggestions/RediscoverPanel.h"
#include "../widgets/suggestions/CrowdEnergyMeter.h"
#include "../../services/history/SessionHistoryRecorder.h"
#include "../../services/history/SessionExporter.h"
#include "../../services/config/SettingsManager.h"
#include "../dialogs/BeatMateLiveWindow.h"
#include "../../services/streaming/ChartsService.h"
#include "../widgets/suggestions/LivePreviewPlayer.h"

#include <cmath>
#include <cctype>
#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <thread>
#include <memory>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

namespace BeatMate::UI {

namespace {

struct ParsedCamelot { int num = 0; char letter = 'A'; bool valid = false; };

ParsedCamelot parseCamelotStr(const juce::String& raw)
{
    ParsedCamelot p;
    const auto s = raw.trim().toUpperCase().toStdString();
    if (s.size() < 2 || s.size() > 3) return p;
    const char last = s.back();
    if (last != 'A' && last != 'B') return p;
    for (size_t i = 0; i + 1 < s.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return p;
    const int n = std::atoi(s.substr(0, s.size() - 1).c_str());
    if (n < 1 || n > 12) return p;
    p.num = n;
    p.letter = last;
    p.valid = true;
    return p;
}

struct CamelotRelation { float score = 0.5f; juce::String label; bool known = false; };

CamelotRelation camelotRelation(const juce::String& fromKey, const juce::String& toKey)
{
    CamelotRelation r;
    const auto a = parseCamelotStr(fromKey);
    const auto b = parseCamelotStr(toKey);
    if (!a.valid || !b.valid) return r;
    r.known = true;
    const int up = ((b.num - a.num) % 12 + 12) % 12;
    const int circ = juce::jmin(up, 12 - up);
    const bool sameLetter = (a.letter == b.letter);
    const juce::String arrow = " (" + juce::String(a.num) + juce::String::charToString(a.letter)
        + juce::String::fromUTF8("\xe2\x86\x92") + juce::String(b.num) + juce::String::charToString(b.letter) + ")";

    if (sameLetter && up == 0)       { r.score = 1.0f;  r.label = juce::String::fromUTF8("M\xc3\xaame tonalit\xc3\xa9") + arrow; }
    else if (!sameLetter && up == 0) { r.score = 0.94f; r.label = "Relative" + arrow; }
    else if (sameLetter && up == 1)  { r.score = 0.95f; r.label = "Quinte juste" + arrow; }
    else if (sameLetter && up == 11) { r.score = 0.92f; r.label = "Quarte juste" + arrow; }
    else if (sameLetter && up == 2)  { r.score = 0.78f; r.label = juce::String::fromUTF8("Boost \xc3\xa9nergie +2") + arrow; }
    else if (!sameLetter && ((a.letter == 'A' && up == 3) || (a.letter == 'B' && up == 9)))
                                     { r.score = 0.72f; r.label = juce::String::fromUTF8("Parall\xc3\xa8le (ambiance)") + arrow; }
    else if (!sameLetter && (up == 1 || up == 11))
                                     { r.score = 0.66f; r.label = "Diagonale" + arrow; }
    else if (sameLetter && up == 7)  { r.score = 0.62f; r.label = "Jaw's +7" + arrow; }
    else
    {
        r.score = juce::jmax(0.05f, 0.38f - 0.06f * static_cast<float>(circ));
        r.label = juce::String::fromUTF8("Cl\xc3\xa9 \xc3\xa9loign\xc3\xa9""e") + arrow;
    }
    return r;
}

struct BpmFit { float score = 0.5f; juce::String label; bool known = false; };

BpmFit bpmFit(double refBpm, double candBpm)
{
    BpmFit f;
    if (refBpm <= 0.0 || candBpm <= 0.0) return f;
    f.known = true;
    const double options[3] = { candBpm, candBpm * 2.0, candBpm * 0.5 };
    int best = 0;
    double bestDev = std::abs(options[0] / refBpm - 1.0);
    for (int i = 1; i < 3; ++i)
    {
        const double dev = std::abs(options[i] / refBpm - 1.0);
        if (dev < bestDev) { bestDev = dev; best = i; }
    }
    const double pct = (options[best] / refBpm - 1.0) * 100.0;
    const double apct = std::abs(pct);
    f.score = apct <= 4.0
        ? 1.0f - static_cast<float>(apct / 4.0) * 0.15f
        : juce::jmax(0.0f, 0.85f - static_cast<float>(apct - 4.0) * 0.09f);
    f.label = (pct >= 0 ? "+" : "") + juce::String(pct, 1) + "% BPM";
    if (best == 1) f.label += juce::String::fromUTF8(" (\xc3\x97""2)");
    else if (best == 2) f.label += juce::String::fromUTF8(" (\xc3\xb7""2)");
    return f;
}

} // namespace

class BeatMateLiveView::RowHover : public juce::MouseListener
{
public:
    RowHover(juce::ListBox& box, int& hoveredRowRef)
        : box_(box), hovered_(hoveredRowRef)
    {
        box_.addMouseListener(this, true);
    }

    ~RowHover() override { box_.removeMouseListener(this); }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const auto pos = e.getEventRelativeTo(&box_).getPosition();
        const int row = box_.getRowContainingPosition(pos.x, pos.y);
        if (row != hovered_)
        {
            hovered_ = row;
            box_.repaint();
        }
    }

    void mouseExit(const juce::MouseEvent& e) override
    {
        const auto pos = e.getEventRelativeTo(&box_).getPosition();
        if (box_.getLocalBounds().contains(pos)) return;
        if (hovered_ != -1)
        {
            hovered_ = -1;
            box_.repaint();
        }
    }

private:
    juce::ListBox& box_;
    int& hovered_;
};

class BeatMateLiveView::RowDragSource : public juce::MouseListener
{
public:
    RowDragSource(juce::ListBox& box,
                  std::function<juce::String(const juce::MouseEvent&)> resolvePath)
        : box_(box), resolvePath_(std::move(resolvePath))
    {
        box_.addMouseListener(this, true);
    }

    ~RowDragSource() override { box_.removeMouseListener(this); }

    void mouseDown(const juce::MouseEvent& e) override
    {
        started_ = false;
        path_ = resolvePath_ ? resolvePath_(e) : juce::String();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (started_ || path_.isEmpty()) return;
        if (e.getDistanceFromDragStart() < 14) return;
        started_ = true;
        if (!juce::File::isAbsolutePath(path_) || !juce::File(path_).existsAsFile()) return;
        juce::DragAndDropContainer::performExternalDragDropOfFiles(
            juce::StringArray { path_ }, false);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        started_ = false;
        path_.clear();
    }

private:
    juce::ListBox& box_;
    std::function<juce::String(const juce::MouseEvent&)> resolvePath_;
    bool started_ = false;
    juce::String path_;
};

class BeatMateLiveView::SetQueuePanel : public juce::Component,
                                        public juce::DragAndDropContainer,
                                        public juce::DragAndDropTarget
{
public:
    explicit SetQueuePanel(std::vector<SuggestionEntry>& queue) : queue_(queue)
    {
        model_.panel = this;
        list_ = std::make_unique<juce::ListBox>("SetQueue", &model_);
        list_->setRowHeight(40);
        list_->setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list_->setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        list_->setTooltip(juce::String::fromUTF8("Glisser la poign\xc3\xa9""e \xe2\x89\xa1 pour r\xc3\xa9ordonner \xc2\xb7 glisser la ligne vers votre logiciel DJ \xc2\xb7 \xe2\x9c\x95 pour retirer"));
        addAndMakeVisible(*list_);
        list_->addMouseListener(this, true);
        collapsed_ = Prefs::getBool("live.queueCollapsed", false);
    }

    ~SetQueuePanel() override
    {
        if (list_) list_->removeMouseListener(this);
    }

    std::function<void()> onChanged;
    std::function<void(int)> onPlay;
    std::function<void(int)> onSend;

    void setHeadLine(const juce::String& text)
    {
        if (headLine_ == text) return;
        headLine_ = text;
        repaint();
    }

    int desiredHeight() const
    {
        if (collapsed_ || queue_.empty()) return 26;
        return 26 + juce::jmin(164, static_cast<int>(queue_.size()) * 40 + 4);
    }

    void refreshList()
    {
        if (list_) { list_->updateContent(); list_->repaint(); }
        repaint();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        headerBounds_ = area.removeFromTop(26);
        if (list_)
        {
            list_->setVisible(!collapsed_ && !queue_.empty());
            list_->setBounds(area.reduced(2, 0));
        }
    }

    void paint(juce::Graphics& g) override
    {
        auto hb = headerBounds_.toFloat();
        g.setColour(Colors::bgCard());
        g.fillRoundedRectangle(hb, 6.0f);
        g.setColour(Colors::borderLight().withAlpha(0.5f));
        g.drawRoundedRectangle(hb.reduced(0.5f), 6.0f, 1.0f);

        auto inner = headerBounds_.reduced(8, 0);
        g.setColour(Colors::textSecondary());
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f).withStyle("Bold")));
        g.drawText(collapsed_ ? juce::String::fromUTF8("\xe2\x96\xb8") : juce::String::fromUTF8("\xe2\x96\xbe"),
                   inner.removeFromLeft(14), juce::Justification::centredLeft);
        g.setColour(Colors::textPrimary());
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f).withStyle("Bold")));
        g.drawText(juce::String::fromUTF8("\xc3\x80 SUIVRE (") + juce::String(queue_.size()) + ")",
                   inner.removeFromLeft(92), juce::Justification::centredLeft);
        if (headLine_.isNotEmpty())
        {
            g.setColour(Colors::success());
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.5f).withStyle("Bold")));
            g.drawText(headLine_, inner, juce::Justification::centredRight, true);
        }
        else if (queue_.empty())
        {
            g.setColour(Colors::textMuted());
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
            g.drawText(juce::String::fromUTF8("Envoyer copie le titre pour votre logiciel DJ"),
                       inner, juce::Justification::centredRight, true);
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (e.eventComponent == this && headerBounds_.contains(e.getPosition()))
        {
            collapsed_ = !collapsed_;
            Prefs::setBool("live.queueCollapsed", collapsed_);
            resized();
            if (onChanged) onChanged();
        }
        dragRow_ = -1;
        externalStarted_ = false;
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        externalStarted_ = false;
        dragRow_ = -1;
        if (!list_ || e.eventComponent == this) return;
        const auto pos = e.getEventRelativeTo(list_.get()).getPosition();
        const int row = list_->getRowContainingPosition(pos.x, pos.y);
        if (row < 0 || row >= static_cast<int>(queue_.size())) return;
        dragRow_ = row;
        lastMouseDownX_ = pos.x;
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (externalStarted_ || dragRow_ < 0 || lastMouseDownX_ <= 26) return;
        if (e.getDistanceFromDragStart() < 14) return;
        externalStarted_ = true;
        if (dragRow_ >= static_cast<int>(queue_.size())) return;
        const auto path = queue_[static_cast<size_t>(dragRow_)].filePath;
        if (path.isEmpty() || !juce::File::isAbsolutePath(path) || !juce::File(path).existsAsFile()) return;
        juce::DragAndDropContainer::performExternalDragDropOfFiles(
            juce::StringArray { path }, false);
    }

    bool isInterestedInDragSource(const SourceDetails& d) override
    {
        return d.description.isInt() && d.sourceComponent == list_.get();
    }

    void itemDropped(const SourceDetails& d) override
    {
        const int from = static_cast<int>(d.description);
        if (from < 0 || from >= static_cast<int>(queue_.size()) || !list_) return;
        const auto pos = list_->getLocalPoint(this, d.localPosition.toInt());
        int to = list_->getInsertionIndexForPosition(pos.x, pos.y);
        if (to < 0) to = static_cast<int>(queue_.size());
        auto entry = queue_[static_cast<size_t>(from)];
        queue_.erase(queue_.begin() + from);
        if (to > from) --to;
        to = juce::jlimit(0, static_cast<int>(queue_.size()), to);
        queue_.insert(queue_.begin() + to, std::move(entry));
        refreshList();
        if (onChanged) onChanged();
    }

    void removeRow(int row)
    {
        if (row < 0 || row >= static_cast<int>(queue_.size())) return;
        queue_.erase(queue_.begin() + row);
        refreshList();
        resized();
        if (onChanged) onChanged();
    }

    int lastMouseDownX() const { return lastMouseDownX_; }

private:
    class QueueModel : public juce::ListBoxModel
    {
    public:
        SetQueuePanel* panel = nullptr;

        int getNumRows() override
        {
            return panel ? static_cast<int>(panel->queue_.size()) : 0;
        }

        juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override
        {
            if (!panel || rows.isEmpty() || panel->lastMouseDownX() > 26) return {};
            return juce::var(rows[0]);
        }

        void paintListBoxItem(int rowNumber, juce::Graphics& g,
                              int width, int height, bool rowIsSelected) override
        {
            if (!panel || rowNumber < 0 || rowNumber >= static_cast<int>(panel->queue_.size()))
                return;
            const auto& q = panel->queue_[static_cast<size_t>(rowNumber)];

            if (rowIsSelected)
            {
                juce::ColourGradient selGrad(Colors::primary().withAlpha(0.22f), 0.0f, 0.0f,
                                             Colors::primary().withAlpha(0.08f), static_cast<float>(width), 0.0f, false);
                g.setGradientFill(selGrad);
                g.fillRect(0, 0, width, height);
                g.setColour(Colors::primary());
                g.fillRect(0, 0, 3, height);
            }
            else if (rowNumber % 2 == 0)
            {
                g.setColour(Colors::glassWhite());
                g.fillRect(0, 0, width, height);
            }
            if (!rowIsSelected)
            {
                g.setColour(Colors::border().withAlpha(0.35f));
                g.fillRect(Spacing::md, height - 1, width - Spacing::md * 2, 1);
            }

            g.setColour(Colors::textMuted());
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(14.0f).withStyle("Bold")));
            g.drawText(juce::String::fromUTF8("\xe2\x89\xa1"), 0, 0, 24, height, juce::Justification::centred);

            const int rightColW = 196;
            g.setColour(q.alreadyPlayed ? Colors::textPrimary().withAlpha(0.5f) : Colors::textPrimary());
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f).withStyle("Bold")));
            g.drawText(juce::String(rowNumber + 1) + ". " + q.title,
                       28, 3, width - rightColW - 28, height / 2 - 3,
                       juce::Justification::centredLeft, true);
            g.setColour(Colors::textSecondary());
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.5f)));
            g.drawText(q.artist, 28, height / 2, width - rightColW - 28, height / 2 - 4,
                       juce::Justification::centredLeft, true);

            const float badgeY = (static_cast<float>(height) - 18.0f) * 0.5f;
            ProDraw::badge(g, q.bpm > 0.0 ? juce::String(q.bpm, 1) : juce::String::fromUTF8("\xe2\x80\x94"),
                           static_cast<float>(width - 190), badgeY, 50.0f, 18.0f, Colors::bpmBadge());
            if (q.key.isNotEmpty())
                ProDraw::badge(g, q.key, static_cast<float>(width - 134), badgeY, 40.0f, 18.0f,
                               Colors::keyBadge());

            g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
            g.setColour(Colors::textPrimary());
            g.drawText(juce::String::fromUTF8("\xe2\x96\xb6"), width - 86, 0, 24, height, juce::Justification::centred);
            g.setColour(Colors::success());
            g.drawText(juce::String::fromUTF8("\xe2\x86\x97"), width - 58, 0, 24, height, juce::Justification::centred);
            g.setColour(Colors::error().withAlpha(0.85f));
            g.drawText(juce::String::fromUTF8("\xe2\x9c\x95"), width - 30, 0, 24, height, juce::Justification::centred);
        }

        void listBoxItemClicked(int row, const juce::MouseEvent& e) override
        {
            if (!panel || row < 0 || row >= static_cast<int>(panel->queue_.size())) return;
            const int w = e.eventComponent != nullptr ? e.eventComponent->getWidth() : 0;
            const int x = e.getPosition().x;
            if (w <= 0) return;
            if (x >= w - 30)      panel->removeRow(row);
            else if (x >= w - 58) { if (panel->onSend) panel->onSend(row); }
            else if (x >= w - 86) { if (panel->onPlay) panel->onPlay(row); }
        }
    };

    std::vector<SuggestionEntry>& queue_;
    QueueModel model_;
    std::unique_ptr<juce::ListBox> list_;
    juce::Rectangle<int> headerBounds_;
    juce::String headLine_;
    bool collapsed_ = false;
    int dragRow_ = -1;
    int lastMouseDownX_ = 0;
    bool externalStarted_ = false;
};


class BeatMateLiveView::SuggestionsTabContent : public juce::Component
{
public:
    SuggestionsTabContent(juce::ComboBox* profileCb, juce::TextButton* profileSaveBtn,
                          juce::ListBox* list, Widgets::SuggestionPanel* smart,
                          SetQueuePanel* queue, std::function<bool()> isCabin)
        : profileCb_(profileCb), profileSaveBtn_(profileSaveBtn),
          list_(list), smart_(smart),
          queue_(queue), isCabin_(std::move(isCabin)) {}

    void resized() override
    {
        auto area = getLocalBounds().reduced(6);
        if (queue_)
        {
            const int qh = juce::jmin(queue_->desiredHeight(), area.getHeight() / 2);
            queue_->setBounds(area.removeFromBottom(qh));
            area.removeFromBottom(4);
        }
        const bool cabin = isCabin_ && isCabin_();
        if (profileCb_)      profileCb_->setVisible(!cabin);
        if (profileSaveBtn_) profileSaveBtn_->setVisible(!cabin);
        if (!cabin)
        {
            auto profileRow = area.removeFromTop(30);
            if (profileSaveBtn_)
                profileSaveBtn_->setBounds(profileRow.removeFromRight(96).reduced(2, 2));
            if (profileCb_)
                profileCb_->setBounds(profileRow.removeFromRight(
                    juce::jmin(200, juce::jmax(0, profileRow.getWidth()))).reduced(2, 2));
            area.removeFromTop(4);
            if (smart_ && smart_->isVisible())
            {
                smart_->setBounds(area);
                return;
            }
        }
        if (list_) list_->setBounds(area);
    }

    void paint(juce::Graphics& g) override
    {
        const bool smartShown = smart_ && smart_->isVisible();
        if (!smartShown && list_ && list_->isVisible()
            && list_->getModel() && list_->getModel()->getNumRows() == 0)
        {
            auto area = getLocalBounds().reduced(18).withTrimmedTop(48);
            ProDraw::emptyState(g, area, juce::String::fromUTF8("\xe2\x99\xab"),
                                BM_TJ("live.suggestions.empty"),
                                BM_TJ("live.suggestions.emptyDetail"));
        }
    }

private:
    juce::ComboBox*   profileCb_  = nullptr;
    juce::TextButton* profileSaveBtn_ = nullptr;
    juce::ListBox*    list_       = nullptr;
    Widgets::SuggestionPanel* smart_ = nullptr;
    SetQueuePanel*    queue_      = nullptr;
    std::function<bool()> isCabin_;
};

class BeatMateLiveView::TrendingTabContent : public juce::Component
{
public:
    TrendingTabContent(juce::ComboBox* genre, juce::ComboBox* country,
                       juce::ComboBox* sort, juce::TextButton* refresh,
                       juce::Label* dateLbl, juce::ListBox* list)
        : genre_(genre), country_(country), sort_(sort), refresh_(refresh),
          dateLbl_(dateLbl), list_(list) {}

    std::function<void()> onShown;

    void visibilityChanged() override
    {
        if (isVisible() && onShown) onShown();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(6);
        auto filterRow = area.removeFromTop(30);
        if (country_) country_->setBounds(filterRow.removeFromLeft(150).reduced(2, 2));
        filterRow.removeFromLeft(6);
        if (genre_)   genre_  ->setBounds(filterRow.removeFromLeft(130).reduced(2, 2));
        filterRow.removeFromLeft(6);
        if (refresh_) refresh_->setBounds(filterRow.removeFromRight(92).reduced(2, 2));
        filterRow.removeFromRight(6);
        if (sort_)    sort_   ->setBounds(filterRow.reduced(2, 2));
        auto dateRow = area.removeFromTop(18);
        if (dateLbl_) dateLbl_->setBounds(dateRow.reduced(2, 0));
        area.removeFromTop(2);
        if (list_) list_->setBounds(area);
    }

    void paint(juce::Graphics& g) override
    {
        if (list_ && list_->getModel() && list_->getModel()->getNumRows() == 0)
        {
            auto area = getLocalBounds().reduced(18).withTrimmedTop(42);
            ProDraw::emptyState(g, area, juce::String::fromUTF8("\xe2\x97\x8c"),
                                BM_TJ("live.ranking.syncing"),
                                BM_TJ("live.ranking.syncingDetail"),
                                Colors::accent());
        }
    }

private:
    juce::ComboBox*   genre_   = nullptr;
    juce::ComboBox*   country_ = nullptr;
    juce::ComboBox*   sort_    = nullptr;
    juce::TextButton* refresh_ = nullptr;
    juce::Label*      dateLbl_ = nullptr;
    juce::ListBox*    list_    = nullptr;
};

class BeatMateLiveView::RediscoverTabContent : public juce::Component
{
public:
    explicit RediscoverTabContent(Widgets::RediscoverPanel* panel) : panel_(panel) {}

    void resized() override
    {
        auto area = getLocalBounds();
        if (panel_) panel_->setBounds(area);
    }

private:
    Widgets::RediscoverPanel* panel_ = nullptr;
};

class BeatMateLiveView::HistoryTabContent : public juce::Component
{
public:
    HistoryTabContent(juce::ListBox* list, juce::TextButton* exportBtn)
        : list_(list), exportBtn_(exportBtn) {}

    void resized() override
    {
        auto area = getLocalBounds().reduced(6);
        if (exportBtn_)
        {
            auto row = area.removeFromTop(30);
            exportBtn_->setBounds(row.removeFromRight(170).reduced(2, 2));
            area.removeFromTop(2);
        }
        if (list_) list_->setBounds(area);
    }

    void paint(juce::Graphics& g) override
    {
        if (list_ && list_->getModel() && list_->getModel()->getNumRows() == 0)
        {
            auto area = getLocalBounds().reduced(18);
            g.setColour(juce::Colours::white.withAlpha(0.7f));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(14.0f).withStyle("Bold")));
            g.drawText(BM_TJ("live.history.empty"),
                       area.removeFromTop(24), juce::Justification::centred);
            g.setColour(juce::Colours::white.withAlpha(0.45f));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
            g.drawText(BM_TJ("live.history.emptyDetail"),
                       area, juce::Justification::centredTop);
        }
    }

private:
    juce::ListBox*    list_      = nullptr;
    juce::TextButton* exportBtn_ = nullptr;
};

class BeatMateLiveView::MyStyleTabContent : public juce::Component
{
public:
    explicit MyStyleTabContent(Services::Library::TrackDataProvider* provider)
        : provider_(provider)
    {
        startButton_ = std::make_unique<juce::TextButton>(BM_TJ("live.startPlaying"));
        startButton_->setColour(juce::TextButton::buttonColourId, Colors::vuGreen());
        startButton_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        startButton_->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        startButton_->onClick = [this]
        {
            if (auto* live = findParentComponentOfClass<BeatMateLiveView>())
            {
                if (live->rightTabs_) live->rightTabs_->setCurrentTabIndex(0);
                if (live->connectButton_ && live->connectButton_->isVisible())
                    live->connectButton_->triggerClick();
            }
        };
        addChildComponent(*startButton_);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced(14);
        if (startButton_ && startButton_->isVisible())
        {
            auto btnRow = b.removeFromBottom(40);
            startButton_->setBounds(btnRow.withSizeKeepingCentre(220, 36));
        }
    }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().reduced(14);

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(16.0f).withStyle("Bold")));
        g.drawText(BM_TJ("live.myStyle"), area.removeFromTop(24), juce::Justification::centredLeft);
        area.removeFromTop(6);

        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
        g.drawText(BM_TJ("live.myStyle.subtitle"),
                   area.removeFromTop(16), juce::Justification::centredLeft);
        area.removeFromTop(12);

        if (!provider_)
        {
            if (startButton_ && !startButton_->isVisible())
            {
                startButton_->setVisible(true);
                resized();
            }
            g.setColour(juce::Colours::white.withAlpha(0.55f));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f)));
            g.drawText(BM_TJ("live.myStyle.noData"),
                       area.removeFromTop(24), juce::Justification::centred);
            g.setColour(juce::Colours::white.withAlpha(0.4f));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
            g.drawText(BM_TJ("live.myStyle.noDataDetail"),
                       area, juce::Justification::centredTop);
            return;
        }

        if (!statsComputed_)
        {
            startStatsJob();
            g.setColour(Colors::textSecondary());
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f)));
            g.drawText(juce::String::fromUTF8("Analyse de la biblioth\xc3\xa8que en cours\xe2\x80\xa6"),
                       area.removeFromTop(24), juce::Justification::centred);
            return;
        }

        if (totalTracks_ == 0)
        {
            if (startButton_ && !startButton_->isVisible())
            {
                startButton_->setVisible(true);
                resized();
            }
            g.setColour(juce::Colours::white.withAlpha(0.55f));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f)));
            g.drawText(BM_TJ("live.myStyle.emptyLib"),
                       area.removeFromTop(24), juce::Justification::centred);
            g.setColour(juce::Colours::white.withAlpha(0.4f));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
            g.drawText(BM_TJ("live.myStyle.emptyLibDetail"),
                       area, juce::Justification::centredTop);
            return;
        }

        if (startButton_ && startButton_->isVisible())
        {
            startButton_->setVisible(false);
            resized();
        }

        auto drawStat = [&](const juce::String& label, const juce::String& value)
        {
            auto row = area.removeFromTop(26);
            g.setColour(juce::Colours::white.withAlpha(0.6f));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
            g.drawText(label, row.removeFromLeft(180), juce::Justification::centredLeft);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f).withStyle("Bold")));
            g.drawText(value, row, juce::Justification::centredLeft);
            area.removeFromTop(4);
        };

        drawStat(BM_TJ("live.stat.totalPlays"),      juce::String(totalPlays_));
        drawStat(BM_TJ("live.stat.collectionTracks"),juce::String(totalTracks_));
        drawStat(BM_TJ("live.stat.avgBpm"),          juce::String(avgBPM_, 1));
        drawStat(BM_TJ("live.stat.avgEnergy"),       juce::String(avgEnergy_, 2) + " / 10");
        drawStat(BM_TJ("live.stat.dominantCamelot"), dominantCamelot_);

        area.removeFromTop(10);
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f).withStyle("Bold")));
        g.drawText(BM_TJ("live.stat.topGenres"), area.removeFromTop(20), juce::Justification::centredLeft);
        area.removeFromTop(4);

        for (auto& g2 : topGenres_)
        {
            auto row = area.removeFromTop(20);
            g.setColour(juce::Colours::white.withAlpha(0.85f));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
            g.drawText(g2.first, row.removeFromLeft(180), juce::Justification::centredLeft);
            g.setColour(juce::Colours::white.withAlpha(0.65f));
            g.drawText(juce::String(g2.second) + " " + BM_TJ("live.stat.tracks"), row, juce::Justification::centredLeft);
        }
    }

    void refresh() { statsComputed_ = false; repaint(); }

    void setProvider(Services::Library::TrackDataProvider* p)
    {
        provider_ = p;
        statsComputed_ = false;
        statsComputing_ = false;
        repaint();
    }

private:
    struct Stats
    {
        int totalPlays = 0;
        int totalTracks = 0;
        double avgBPM = 0.0;
        float avgEnergy = 0.0f;
        juce::String dominantCamelot = "-";
        std::vector<std::pair<juce::String, int>> topGenres;
    };

    static Stats computeStatsFor(Services::Library::TrackDataProvider* provider)
    {
        Stats s;
        if (!provider) return s;

        auto all = provider->getAllTracks();
        s.totalTracks = static_cast<int>(all.size());
        if (all.empty()) return s;

        double sumBPM = 0.0;
        double sumEnergy = 0.0;
        int nBPM = 0, nEn = 0;
        std::map<std::string, int> genreCounts;
        std::map<std::string, int> camelotCounts;

        for (const auto& t : all)
        {
            s.totalPlays += t.playCount;
            if (t.bpm > 0.0)    { sumBPM += t.bpm; ++nBPM; }
            if (t.energy > 0.0f){ sumEnergy += t.energy; ++nEn; }
            if (!t.genre.empty()) genreCounts[t.genre]++;
            if (!t.camelotKey.empty()) camelotCounts[t.camelotKey]++;
        }

        s.avgBPM    = nBPM ? sumBPM / nBPM : 0.0;
        s.avgEnergy = nEn  ? static_cast<float>(sumEnergy / nEn) : 0.0f;

        std::vector<std::pair<std::string, int>> sorted(genreCounts.begin(), genreCounts.end());
        std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        for (size_t i = 0; i < sorted.size() && i < 5; ++i)
            s.topGenres.emplace_back(juce::String(sorted[i].first), sorted[i].second);

        if (!camelotCounts.empty())
        {
            auto best = std::max_element(camelotCounts.begin(), camelotCounts.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; });
            s.dominantCamelot = juce::String(best->first);
        }
        return s;
    }

    void startStatsJob()
    {
        if (statsComputing_) return;
        statsComputing_ = true;

        auto* provider = provider_;
        juce::Component::SafePointer<MyStyleTabContent> safe(this);
        auto job = [safe, provider]()
        {
            auto stats = std::make_shared<Stats>(computeStatsFor(provider));
            juce::MessageManager::callAsync([safe, stats]()
            {
                if (auto* self = safe.getComponent())
                    self->applyStats(*stats);
            });
        };

        if (auto* pool = ::BeatMate::getBackgroundPool())
            pool->addJob(job);
        else
            job();
    }

    void applyStats(const Stats& s)
    {
        totalPlays_       = s.totalPlays;
        totalTracks_      = s.totalTracks;
        avgBPM_           = s.avgBPM;
        avgEnergy_        = s.avgEnergy;
        dominantCamelot_  = s.dominantCamelot;
        topGenres_        = s.topGenres;
        statsComputed_    = true;
        statsComputing_   = false;
        repaint();
    }

    Services::Library::TrackDataProvider* provider_ = nullptr;
    bool statsComputed_ = false;
    bool statsComputing_ = false;
    int totalPlays_ = 0;
    int totalTracks_ = 0;
    double avgBPM_ = 0.0;
    float avgEnergy_ = 0.0f;
    juce::String dominantCamelot_ = "-";
    std::vector<std::pair<juce::String, int>> topGenres_;
    std::unique_ptr<juce::TextButton> startButton_;
};




BeatMateLiveView::BeatMateLiveView(Services::Library::TrackDataProvider* provider)
    : BeatMateLiveView()
{
    m_provider = provider;
    if (tabContentMyStyle_)
        tabContentMyStyle_->setProvider(provider);
}

BeatMateLiveView::BeatMateLiveView()
    : m_provider(nullptr)
{
    setOpaque(true);


    alwaysOnTopToggle_ = std::make_unique<juce::ToggleButton>(BM_TJ("live.alwaysOnTop"));
    alwaysOnTopToggle_->setColour(juce::ToggleButton::textColourId, Colors::textSecondary());
    alwaysOnTopToggle_->setColour(juce::ToggleButton::tickColourId, Colors::primary());
    alwaysOnTopToggle_->onClick = [this]
    {
        alwaysOnTop_ = alwaysOnTopToggle_->getToggleState();
        if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
            window->setAlwaysOnTop(alwaysOnTop_);
        Prefs::setBool("live.alwaysOnTop", alwaysOnTop_);
    };
    addAndMakeVisible(*alwaysOnTopToggle_);

    compactToggle_ = std::make_unique<juce::TextButton>(BM_TJ("live.modeCompact"));
    compactToggle_->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    compactToggle_->setColour(juce::TextButton::textColourOffId, Colors::textSecondary());
    compactToggle_->onClick = [this] { toggleCompactMode(); };
    addAndMakeVisible(*compactToggle_);

    settingsButton_ = std::make_unique<juce::TextButton>(BM_TJ("live.settings"));
    settingsButton_->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    settingsButton_->setColour(juce::TextButton::textColourOffId, Colors::textSecondary());
    settingsButton_->setTooltip(juce::String::fromUTF8("R\xc3\xa9glages du mode Live (source, intervalle, masquage auto)"));
    settingsButton_->onClick = [this] { showSettingsPanel(); };
    addAndMakeVisible(*settingsButton_);

    helpButton_ = std::make_unique<juce::TextButton>("?");
    helpButton_->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    helpButton_->setColour(juce::TextButton::textColourOffId, Colors::textSecondary());
    helpButton_->setTooltip(juce::String::fromUTF8("Aide du mode Live"));
    helpButton_->onClick = [this] { showHelpPopup(); };
    addAndMakeVisible(*helpButton_);


    importRekordboxXmlButton_ = std::make_unique<juce::TextButton>(BM_TJ("live.importXml"));
    importRekordboxXmlButton_->setColour(juce::TextButton::buttonColourId,
                                         juce::Colour(0xFF8B5CF6));
    importRekordboxXmlButton_->setColour(juce::TextButton::textColourOffId,
                                         juce::Colours::white);
    importRekordboxXmlButton_->setTooltip(BM_TJ("live.importXmlTooltip"));
    importRekordboxXmlButton_->onClick = [this] { importRekordboxXmlFlow(); };
    addAndMakeVisible(*importRekordboxXmlButton_);

    connectButton_ = std::make_unique<juce::TextButton>(BM_TJ("live.connectPrefix") + " " + currentSource_);
    connectButton_->setColour(juce::TextButton::buttonColourId, Colors::vuGreen());
    connectButton_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    connectButton_->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    connectButton_->setTooltip(BM_TJ("live.connectTooltip"));
    connectButton_->onClick = [this] { handleConnectClicked(); };
    addAndMakeVisible(*connectButton_);

    disconnectButton_ = std::make_unique<juce::TextButton>(BM_TJ("live.disconnect"));
    disconnectButton_->setColour(juce::TextButton::buttonColourId, Colors::error().darker(0.35f));
    disconnectButton_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    disconnectButton_->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    disconnectButton_->setTooltip(BM_TJ("live.disconnectTooltip"));
    disconnectButton_->onClick = [this] { handleDisconnectClicked(); };
    disconnectButton_->setVisible(false);
    addAndMakeVisible(*disconnectButton_);

    compactBackButton_ = std::make_unique<juce::TextButton>(BM_TJ("live.modeFull"));
    compactBackButton_->setColour(juce::TextButton::buttonColourId, Colors::primary());
    compactBackButton_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    compactBackButton_->onClick = [this] { toggleCompactMode(); };
    compactBackButton_->setVisible(false);
    addAndMakeVisible(*compactBackButton_);

    sourceSelector_ = std::make_unique<juce::ComboBox>();
    sourceSelector_->addItem(juce::String(juce::CharPointer_UTF8("\xF0\x9F\x8E\xA7 Rekordbox")), 1);
    sourceSelector_->addItem(juce::String(juce::CharPointer_UTF8("\xF0\x9F\x94\x8A Serato")), 2);
    sourceSelector_->addItem(juce::String(juce::CharPointer_UTF8("\xF0\x9F\x8E\x9B\xEF\xB8\x8F Traktor")), 3);
    sourceSelector_->addItem(juce::String(juce::CharPointer_UTF8("\xF0\x9F\x92\xBF VirtualDJ")), 4);
    sourceSelector_->addItem(juce::String(juce::CharPointer_UTF8("\xF0\x9F\x8E\xB6 djay Pro")), 5);
    sourceSelector_->addItem(juce::String(juce::CharPointer_UTF8("\xF0\x9F\x9A\x82 Engine DJ")), 6);
    sourceSelector_->addItem(juce::String(juce::CharPointer_UTF8("\xF0\x9F\xA4\x96 AUDIO (IA)")), 7);
    sourceSelector_->setSelectedId(4);
    sourceSelector_->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    sourceSelector_->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    sourceSelector_->setColour(juce::ComboBox::outlineColourId, Colors::borderLight());
    sourceSelector_->setColour(juce::ComboBox::arrowColourId, Colors::textMuted());
    sourceSelector_->setTooltip(juce::String::fromUTF8("Logiciel DJ \xc3\xa0 surveiller en temps r\xc3\xa9""el"));
    sourceSelector_->onChange = [this]
    {
        switch (sourceSelector_->getSelectedId())
        {
            case 1: currentSource_ = "Rekordbox"; break;
            case 2: currentSource_ = "Serato";    break;
            case 3: currentSource_ = "Traktor";   break;
            case 4: currentSource_ = "VirtualDJ"; break;
            case 5: currentSource_ = "djay Pro";  break;
            case 6: currentSource_ = "Engine DJ"; break;
            case 7: currentSource_ = "AUDIO";     break;
            default: currentSource_ = "VirtualDJ"; break;
        }
        Prefs::setInt("live.sourceSelectorId", sourceSelector_->getSelectedId());
        Prefs::setString("live.currentSource", currentSource_.toStdString());
        lastDetectedTitle_.clear();
        activeDetectedSource_.clear();
        if (nowPlayingSvc_) {
            nowPlayingSvc_->setPreferredSource(currentSource_.toStdString());
            nowPlayingSvc_->pollNow();
        }

        if (connectButton_)
            connectButton_->setButtonText(BM_TJ("live.connectPrefix") + " " + currentSource_);
        if (disconnectButton_)
            disconnectButton_->setButtonText(BM_TJ("live.disconnect") + " " + currentSource_);

        // Switching DJ software = force disconnection; user must click
        if (monitoring_) stopMonitoring();
        nowPlaying_.connected = false;
        if (connectButton_)    connectButton_->setVisible(true);
        if (disconnectButton_) disconnectButton_->setVisible(false);
        resized();
        repaint();
    };
    addAndMakeVisible(*sourceSelector_);

    profileLbl_ = std::make_unique<juce::Label>("djprofLbl", BM_TJ("live.djProfile"));
    profileLbl_->setFont(juce::Font(11.0f));
    profileLbl_->setColour(juce::Label::textColourId, Colors::textMuted());
    addAndMakeVisible(*profileLbl_);

    profileCb_ = std::make_unique<juce::ComboBox>("djprof");
    profileCb_->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    profileCb_->setColour(juce::ComboBox::textColourId,       Colors::textPrimary());
    profileCb_->setColour(juce::ComboBox::outlineColourId,    Colors::borderLight());
    profileCb_->setColour(juce::ComboBox::arrowColourId,      Colors::textMuted());
    profileCb_->setTooltip(BM_TJ("live.profileTooltip"));
    profileCb_->onChange = [this] { onProfileSelected(); };
    addAndMakeVisible(*profileCb_);

    profileSaveAsBtn_ = std::make_unique<juce::TextButton>(BM_TJ("live.saveAs"));
    profileSaveAsBtn_->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    profileSaveAsBtn_->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    profileSaveAsBtn_->setTooltip(BM_TJ("live.profileSaveTooltip"));
    profileSaveAsBtn_->onClick = [this] { onSaveProfileAs(); };
    addAndMakeVisible(*profileSaveAsBtn_);

    rebuildProfileCombo();

    tabSuggestions_ = std::make_unique<juce::TextButton>(BM_TJ("live.suggestions"));
    tabSuggestions_->setColour(juce::TextButton::buttonColourId, Colors::primary());
    tabSuggestions_->onClick = [this] { switchBottomTab(BottomTab::Suggestions); };
    addAndMakeVisible(*tabSuggestions_);

    tabTrending_ = std::make_unique<juce::TextButton>(BM_TJ("live.tab.trending"));
    tabTrending_->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    tabTrending_->onClick = [this] { switchBottomTab(BottomTab::Trending); };
    addAndMakeVisible(*tabTrending_);

    tabHistory_ = std::make_unique<juce::TextButton>(BM_TJ("live.tab.history"));
    tabHistory_->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    tabHistory_->onClick = [this] { switchBottomTab(BottomTab::History); };
    addAndMakeVisible(*tabHistory_);

    genreFilter_ = std::make_unique<juce::ComboBox>();
    genreFilter_->addItem(BM_TJ("live.allGenres"), 1);
    genreFilter_->addItem("Pop",             2);
    genreFilter_->addItem("Rock",            3);
    genreFilter_->addItem("Funk",            4);
    genreFilter_->addItem("Soul",            5);
    genreFilter_->addItem("Disco",           6);
    genreFilter_->addItem("Motown",          7);
    genreFilter_->addItem("Hip Hop",         8);
    genreFilter_->addItem("R&B",             9);
    genreFilter_->addItem("House",           10);
    genreFilter_->addItem("Tech House",      11);
    genreFilter_->addItem("Techno",          12);
    genreFilter_->addItem("Trance",          13);
    genreFilter_->addItem("EDM",             14);
    genreFilter_->addItem("Electro",         15);
    genreFilter_->addItem("Drum & Bass",     16);
    genreFilter_->addItem("Dubstep",         17);
    genreFilter_->addItem("Latin",           18);
    genreFilter_->addItem("Reggaeton",       19);
    genreFilter_->addItem("Reggae",          20);
    genreFilter_->addItem("Dancehall",       21);
    genreFilter_->addItem("Zouk",            22);
    genreFilter_->addItem("Kompa",           23);
    genreFilter_->addItem("Afrobeat",        24);
    genreFilter_->addItem("Amapiano",        25);
    genreFilter_->addItem("Jazz",            26);
    genreFilter_->setSelectedId(1);
    genreFilter_->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    genreFilter_->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    genreFilter_->setColour(juce::ComboBox::outlineColourId, Colors::borderLight());
    genreFilter_->setTooltip(juce::String::fromUTF8("Filtrer le classement par genre"));
    genreFilter_->onChange = [this] {
        Prefs::setInt("live.trendingGenreId", genreFilter_->getSelectedId());
        if (countryFilter_ && countryFilter_->getSelectedId() != 1) return;
        loadTrendingFromCollection();
    };
    addAndMakeVisible(*genreFilter_);

    countryFilter_ = std::make_unique<juce::ComboBox>();
    countryFilter_->addItem(BM_TJ("live.localCollection"), 1);
    countryFilter_->addItem("France (FR)",       2);
    countryFilter_->addItem("USA (US)",          3);
    countryFilter_->addItem("UK (GB)",           4);
    countryFilter_->addItem("Allemagne (DE)",    5);
    countryFilter_->addItem("Japon (JP)",        6);
    countryFilter_->addItem("Bresil (BR)",       7);
    countryFilter_->addItem("Espagne (ES)",      8);
    countryFilter_->addItem("Italie (IT)",       9);
    countryFilter_->addItem("Monde (Global)",    10);
    countryFilter_->setSelectedId(2);
    countryFilter_->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    countryFilter_->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    countryFilter_->setColour(juce::ComboBox::outlineColourId, Colors::borderLight());
    countryFilter_->setTooltip(juce::String::fromUTF8("Charts par pays (Deezer/Apple Music), ou collection locale"));
    countryFilter_->onChange = [this]
    {
        Prefs::setInt("live.trendingCountryId", countryFilter_->getSelectedId());
        reloadTrendingSelection(false);
    };
    addAndMakeVisible(*countryFilter_);

    trendingSortCb_ = std::make_unique<juce::ComboBox>();
    trendingSortCb_->addItem(juce::String::fromUTF8("Tri : classement"), 1);
    trendingSortCb_->addItem(juce::String::fromUTF8("Tri : tendances \xe2\x86\x91"), 2);
    trendingSortCb_->setSelectedId(Prefs::getInt("live.trendingSortId", 1) == 2 ? 2 : 1);
    trendingSortCb_->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    trendingSortCb_->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    trendingSortCb_->setColour(juce::ComboBox::outlineColourId, Colors::borderLight());
    trendingSortCb_->setTooltip(juce::String::fromUTF8("Tendances : nouvelles entr\xc3\xa9""es et plus fortes hausses d'abord"));
    trendingSortCb_->onChange = [this] {
        Prefs::setInt("live.trendingSortId", trendingSortCb_->getSelectedId());
        applyTrendingSort();
        if (trendingList_) {
            trendingList_->updateContent();
            trendingList_->repaint();
        }
    };
    addAndMakeVisible(*trendingSortCb_);

    trendingRefreshBtn_ = std::make_unique<juce::TextButton>("Actualiser");
    trendingRefreshBtn_->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    trendingRefreshBtn_->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    trendingRefreshBtn_->setTooltip(juce::String::fromUTF8("Recharger le classement depuis les sources en ligne"));
    trendingRefreshBtn_->onClick = [this] { reloadTrendingSelection(true); };
    addAndMakeVisible(*trendingRefreshBtn_);

    trendingDateLbl_ = std::make_unique<juce::Label>();
    trendingDateLbl_->setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    trendingDateLbl_->setColour(juce::Label::textColourId, Colors::textSecondary());
    trendingDateLbl_->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(*trendingDateLbl_);

    suggestionModel_ = std::make_unique<SuggestionListModel>();
    suggestionModel_->entries = &suggestions_;
    suggestionModel_->onItemClicked = [this](int row, const juce::MouseEvent& e)
    {
        if (e.mods.isRightButtonDown() || e.mods.isShiftDown())
        {
            showSendToDJMenu(row, e.getScreenPosition());
            return;
        }
        if (row < 0 || row >= static_cast<int>(suggestions_.size())) return;
        const auto& s = suggestions_[static_cast<size_t>(row)];
        if (!s.inLibrary && s.previewUrl.isNotEmpty())
            Widgets::LivePreviewPlayer::getInstance().playPreview(
                ("sugg|" + s.artist + "|" + s.title).toStdString(),
                s.artist.toStdString(), s.title.toStdString(),
                s.previewUrl.toStdString());
    };
    suggestionModel_->onItemDoubleClicked = [this](int row) {
        if (row < 0 || row >= (int)suggestions_.size()) return;
        if (!onSendSuggestionToPrep) return;
        const auto& s = suggestions_[(size_t)row];
        SuggestionPayload payload;
        payload.title    = s.title;
        payload.artist   = s.artist;
        payload.filePath = s.filePath;
        payload.bpm      = s.bpm;
        payload.key      = s.key;
        onSendSuggestionToPrep(payload);
    };
    suggestionModel_->onPinClicked = [this](int idx) {
        if (idx >= 0 && idx < static_cast<int>(suggestions_.size()))
            addEntryToQueue(suggestions_[static_cast<size_t>(idx)]);
    };
    suggestionModel_->onPlayClicked = [this](int idx) {
        if (idx >= 0 && idx < static_cast<int>(suggestions_.size()))
            playEntryPreview(suggestions_[static_cast<size_t>(idx)], "sugg");
    };
    suggestionModel_->onSendClicked = [this](int idx) {
        if (idx >= 0 && idx < static_cast<int>(suggestions_.size()))
            showSendToDJMenuForEntry(suggestions_[static_cast<size_t>(idx)]);
    };
    suggestionList_ = std::make_unique<juce::ListBox>("Suggestions", suggestionModel_.get());
    suggestionList_->setRowHeight(48);
    suggestionList_->setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    suggestionList_->setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    suggestionList_->setTooltip(juce::String::fromUTF8("Envoyer : copie \xc2\xab Artiste Titre \xc2\xbb pour la recherche de votre logiciel DJ (Ctrl+F \xe2\x86\x92 Ctrl+V \xe2\x86\x92 Entr\xc3\xa9""e) \xc2\xb7 Clic : pr\xc3\xa9\xc3\xa9""coute \xc2\xb7 Double-clic : envoyer vers Pr\xc3\xa9paration"));
    addAndMakeVisible(*suggestionList_);

    historyModel_ = std::make_unique<HistoryListModel>();
    historyModel_->entries = &history_;
    historyModel_->onSendClicked = [this](int row)
    {
        if (row < 0 || row >= static_cast<int>(history_.size())) return;
        const auto& entry = history_[static_cast<size_t>(row)];
        const auto query = (entry.artist + " " + entry.title).trim();
        if (query.isEmpty()) return;
        juce::SystemClipboard::copyTextToClipboard(query);
        Utils::showAutoCloseAlert(juce::MessageBoxIconType::InfoIcon,
            BM_TJ("live.copy.done") + " : " + query,
            BM_TJ("live.copy.procedure"), 7000);
    };
    historyModel_->onItemDoubleClicked = [this](int row)
    {
        if (row < 0 || row >= static_cast<int>(history_.size())) return;
        if (!m_provider) return;
        const auto entry = history_[static_cast<size_t>(row)];
        auto results = m_provider->searchTracks(entry.title.toStdString());
        if (results.empty())
            results = m_provider->searchTracks(
                (entry.artist + " " + entry.title).toStdString());
        if (results.empty()) return;
        const auto& t = results.front();
        if (t.id > 0)
        {
            refreshSmartSuggestFor(t.id);
        }
        else
        {
            auto compatible = m_provider->getSuggestionsFor(t, 50);
            suggestions_.clear();
            for (const auto& s : compatible)
            {
                SuggestionEntry e;
                e.title  = juce::String(s.title);
                e.artist = juce::String(s.artist);
                e.bpm    = s.bpm;
                e.key    = juce::String(s.key.empty() ? s.camelotKey : s.key);
                e.score  = 0.7f;
                e.relevance = e.score;
                e.streamingSource = (s.source == Models::TrackSource::Streaming)
                    ? "streaming" : "local";
                e.filePath = juce::String(s.filePath);
                e.energy   = s.energy;
                suggestions_.push_back(e);
            }
            updateSuggestions();
        }
        refreshSimilarStreaming(entry.title, entry.artist);
        if (rightTabs_) rightTabs_->setCurrentTabIndex(0);
    };
    historyList_ = std::make_unique<juce::ListBox>("History", historyModel_.get());
    historyList_->setRowHeight(44);
    historyList_->setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    historyList_->setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    historyList_->setTooltip(juce::String::fromUTF8("Double-clic : relancer les suggestions sur cette piste"));
    addAndMakeVisible(*historyList_);

    trendingModel_ = std::make_unique<TrendingListModel>();
    trendingModel_->entries = &trending_;
    trendingList_ = std::make_unique<juce::ListBox>("Trending", trendingModel_.get());
    trendingList_->setRowHeight(50);
    trendingList_->setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    trendingList_->setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    trendingList_->setTooltip(juce::String::fromUTF8("Double-clic : envoi direct (ou retrouver la piste si elle est dans votre biblioth\xc3\xa8que) \xc2\xb7 Clic droit : menu"));
    addAndMakeVisible(*trendingList_);

    suggestionHover_ = std::make_unique<RowHover>(*suggestionList_, suggestionModel_->hoveredRow);
    historyHover_    = std::make_unique<RowHover>(*historyList_,    historyModel_->hoveredRow);
    trendingHover_   = std::make_unique<RowHover>(*trendingList_,   trendingModel_->hoveredRow);

    suggestionDragSource_ = std::make_unique<RowDragSource>(*suggestionList_,
        [this](const juce::MouseEvent& e) -> juce::String
        {
            if (!suggestionList_) return {};
            const auto pos = e.getEventRelativeTo(suggestionList_.get()).getPosition();
            const int row = suggestionList_->getRowContainingPosition(pos.x, pos.y);
            if (row < 0) return {};
            int idx = row;
            if (suggestionModel_ && suggestionModel_->cabin)
            {
                const int half = juce::jmax(1, suggestionList_->getWidth() / 2);
                idx = row * 2 + (pos.x >= half ? 1 : 0);
            }
            if (idx < 0 || idx >= static_cast<int>(suggestions_.size())) return {};
            return suggestions_[static_cast<size_t>(idx)].filePath;
        });

    queuePanel_ = std::make_unique<SetQueuePanel>(setQueue_);
    queuePanel_->onChanged = [this] {
        persistSetQueue();
        updateQueueHeader();
        if (tabContentSuggestions_) tabContentSuggestions_->resized();
    };
    queuePanel_->onPlay = [this](int idx) {
        if (idx >= 0 && idx < static_cast<int>(setQueue_.size()))
            playEntryPreview(setQueue_[static_cast<size_t>(idx)], "queue");
    };
    queuePanel_->onSend = [this](int idx) {
        if (idx >= 0 && idx < static_cast<int>(setQueue_.size()))
            showSendToDJMenuForEntry(setQueue_[static_cast<size_t>(idx)]);
    };
    restoreSetQueue();

    auto sendTrendingTo = [this](int row,
                                 Services::DJSoftware::DJTarget,
                                 Services::DJSoftware::DeckSlot) {
        if (row < 0 || row >= static_cast<int>(trending_.size())) return;
        const auto& e = trending_[static_cast<size_t>(row)];
        const auto query = (e.artist + " " + e.title).trim();
        if (query.isEmpty()) return;
        juce::SystemClipboard::copyTextToClipboard(query);
        Utils::showAutoCloseAlert(juce::MessageBoxIconType::InfoIcon,
            BM_TJ("live.copy.done") + " : " + query,
            BM_TJ("live.copy.procedure"), 7000);
        spdlog::info("[Live HOT100 copy-for-DJ] '{}'", query.toStdString());
    };

    trendingModel_->onSendClicked = [sendTrendingTo](int row) {
        sendTrendingTo(row, Services::DJSoftware::DJTarget::Auto,
                       Services::DJSoftware::DeckSlot::DeckA);
    };
    trendingModel_->onQuickSend = [this, sendTrendingTo](int row) {
        if (row >= 0 && row < static_cast<int>(trending_.size())
            && trending_[static_cast<size_t>(row)].inLibrary)
        {
            locateTrendingInLibrary(row);
            return;
        }
        sendTrendingTo(row,
                       Services::DJSoftware::DJTarget::Auto,
                       Services::DJSoftware::DeckSlot::DeckA);
    };
    trendingModel_->onOpenSendMenu = [this, sendTrendingTo](int row) {
        if (row < 0 || row >= static_cast<int>(trending_.size())) return;
        const bool owned      = trending_[static_cast<size_t>(row)].inLibrary;
        const bool hasPreview = trending_[static_cast<size_t>(row)].previewUrl.isNotEmpty()
                             || trending_[static_cast<size_t>(row)].position > 0;
        using DJT  = Services::DJSoftware::DJTarget;
        using Slot = Services::DJSoftware::DeckSlot;
        juce::PopupMenu menu;
        if (hasPreview)
            menu.addItem(2, juce::String::fromUTF8("Pr\xc3\xa9\xc3\xa9""couter (30 s)"));
        if (owned)
            menu.addItem(3, juce::String::fromUTF8("Retrouver dans la biblioth\xc3\xa8que"));
        if (hasPreview || owned)
            menu.addSeparator();
        menu.addItem(1, BM_TJ("live.menu.copySearch"));
        juce::Component::SafePointer<BeatMateLiveView> safe(this);
        auto onChosen = [safe, row, sendTrendingTo](int result) {
            switch (result) {
                case 1:  sendTrendingTo(row, DJT::Auto,      Slot::DeckA); break;
                case 2:
                    if (auto* self = safe.getComponent();
                        self != nullptr && row < static_cast<int>(self->trending_.size()))
                    {
                        const auto& e = self->trending_[static_cast<size_t>(row)];
                        Widgets::LivePreviewPlayer::getInstance().playPreview(
                            ("hot100|" + e.artist + "|" + e.title).toStdString(),
                            e.artist.toStdString(), e.title.toStdString(),
                            e.previewUrl.toStdString());
                    }
                    break;
                case 3:
                    if (auto* self = safe.getComponent())
                        self->locateTrendingInLibrary(row);
                    break;
                default: break;
            }
        };
        menu.showMenuAsync(juce::PopupMenu::Options()
                               .withTargetComponent(trendingList_.get()),
                           onChosen);
    };

    trendingList_->setVisible(false);
    historyList_->setVisible(false);
    genreFilter_->setVisible(false);
    countryFilter_->setVisible(false);

    generateWaveformData();

    nowPlaying_.connected = false;
    nowPlaying_.title = "---";
    nowPlaying_.artist = "---";
    nowPlaying_.bpm = 0.0;
    nowPlaying_.key = "";
    nowPlaying_.energy = 0.0f;

    suggestions_.clear();
    trending_.clear();
    history_.clear();
    totalTracksPlayed_ = 0;
    totalSetDuration_ = "0:00";
    averageBPM_ = 0.0;

    {
        extern BeatMate::ServiceLocator* g_serviceLocator;
        if (g_serviceLocator)
            smartEngine_ = g_serviceLocator->tryGet<Services::Suggestions::SmartSuggestEngine>();
    }
    smartPanel_ = std::make_unique<Widgets::SuggestionPanel>();
    smartPanel_->setEngine(smartEngine_);
    {
        extern BeatMate::ServiceLocator* g_serviceLocator;
        if (g_serviceLocator)
        {
            if (auto* svc = g_serviceLocator->tryGet<Services::Suggestions::DJProfileService>())
                smartPanel_->setProfileContext(svc, activeProfileName_);
        }
    }
    addAndMakeVisible(*smartPanel_);

    {
        extern BeatMate::ServiceLocator* g_serviceLocator;
        if (g_serviceLocator)
        {
            if (auto* clap = g_serviceLocator->tryGet<Services::AI::ClapEmbedQueue>())
            {
                juce::Component::SafePointer<BeatMateLiveView> safe(this);
                auto* engine = smartEngine_;
                clap->setOnPublish([safe, engine](int, int)
                {
                    if (engine) engine->invalidateCache();
                    juce::MessageManager::callAsync([safe]
                    {
                        auto* self = safe.getComponent();
                        if (!self || !self->smartPanel_ || !self->smartPanel_->isShowing()) return;
                        const double nowMs = juce::Time::getMillisecondCounterHiRes();
                        if (nowMs - self->lastClapAutoRefreshMs_ < 60000.0) return;
                        self->lastClapAutoRefreshMs_ = nowMs;
                        self->smartPanel_->refresh();
                    });
                });
            }
        }
    }

    {
        extern BeatMate::ServiceLocator* g_serviceLocator;
        if (g_serviceLocator)
        {
            if (auto* uhist = g_serviceLocator->tryGet<Services::DJSoftware::UnifiedDJHistory>())
            {
                try {
                    seedingHistory_ = true;
                    auto past = uhist->getRecent(200);
                    for (auto it = past.rbegin(); it != past.rend(); ++it) {
                        juce::String title   = juce::String(it->title);
                        juce::String artist  = juce::String(it->artist);
                        juce::String key     = juce::String(it->camelotKey);
                        addToHistory(title, artist, it->bpm, key, 0.0f);
                    }
                    seedingHistory_ = false;
                } catch (...) { seedingHistory_ = false; }

                juce::Component::SafePointer<BeatMate::UI::BeatMateLiveView> self(this);
                uhist->onNewPlay = [self](const Services::DJSoftware::PlayedTrack& t) {
                    juce::MessageManager::callAsync([self, t]() {
                        if (!self) return;
                        self->addToHistory(juce::String(t.title), juce::String(t.artist),
                                           t.bpm, juce::String(t.camelotKey), 0.0f);
                    });
                };

                uhist->startPolling(10);
            }
        }
    }

    nowPlayingSvc_ = std::make_unique<Services::Live::NowPlayingService>();
    {
        if (auto last = Services::Live::NowPlayingService::loadLastPersisted()) {
            const juce::String lastTitle = juce::String(last->title);
            const bool alreadyTop = !history_.empty() && history_.front().title == lastTitle;
            if (!alreadyTop && lastTitle.isNotEmpty()) {
                seedingHistory_ = true;
                addToHistory(lastTitle, juce::String(last->artist),
                             last->bpm, juce::String(last->key), 0.0f);
                seedingHistory_ = false;
            }
        }
        juce::Component::SafePointer<BeatMateLiveView> safeSvc(this);
        nowPlayingSvc_->onTrackChanged =
            [safeSvc](const Services::Live::NowPlayingTrack& t)
        {
            juce::MessageManager::callAsync([safeSvc, t]() {
                if (auto* self = safeSvc.getComponent())
                    self->applyDetectedTrack(t);
            });
        };
        nowPlayingSvc_->setPreferredSource(currentSource_.toStdString());
        nowPlayingSvc_->start(1000);
    }


    if (tabSuggestions_) tabSuggestions_->setVisible(false);
    if (tabTrending_)    tabTrending_   ->setVisible(false);
    if (tabHistory_)     tabHistory_    ->setVisible(false);

    exportSessionBtn_ = std::make_unique<juce::TextButton>(
        juce::String::fromUTF8("Exporter la session"));
    exportSessionBtn_->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    exportSessionBtn_->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    exportSessionBtn_->setTooltip(juce::String::fromUTF8(
        "Exporter la session courante : M3U8, Rekordbox XML, Serato crate, Traktor NML ou CSV."));
    exportSessionBtn_->onClick = [this] { showSessionExportMenu(); };

    tabContentSuggestions_ = std::make_unique<SuggestionsTabContent>(
        profileCb_.get(), profileSaveAsBtn_.get(),
        suggestionList_.get(), smartPanel_.get(),
        queuePanel_.get(), [this] { return cabinMode_; });
    tabContentTrending_    = std::make_unique<TrendingTabContent>(
        genreFilter_.get(), countryFilter_.get(), trendingSortCb_.get(),
        trendingRefreshBtn_.get(), trendingDateLbl_.get(), trendingList_.get());
    tabContentTrending_->onShown = [this] {
        if (trendingLoadedOnce_) return;
        if (trending_.empty()) loadTrendingFromDiskCache();
        reloadTrendingSelection(false);
    };
    tabContentHistory_     = std::make_unique<HistoryTabContent>(historyList_.get(),
                                                                 exportSessionBtn_.get());
    tabContentMyStyle_     = std::make_unique<MyStyleTabContent>(m_provider);

    rediscoverPanel_ = std::make_unique<Widgets::RediscoverPanel>();
    {
        extern BeatMate::ServiceLocator* g_serviceLocator;
        if (g_serviceLocator)
        {
            if (auto* db = g_serviceLocator->tryGet<Services::Library::TrackDatabase>())
                rediscoverPanel_->setDatabase(db);
            if (auto* router = g_serviceLocator->tryGet<Services::DJSoftware::SendToDJRouter>())
                rediscoverPanel_->setRouter(router);
        }
        if (smartEngine_)
            rediscoverPanel_->setSmartEngine(smartEngine_);
    }
    tabContentRediscover_ = std::make_unique<RediscoverTabContent>(rediscoverPanel_.get());

    rightTabs_ = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
    rightTabs_->setOutline(0);
    rightTabs_->setIndent(4);
    rightTabs_->setColour(juce::TabbedComponent::backgroundColourId, juce::Colours::transparentBlack);
    rightTabs_->setColour(juce::TabbedComponent::outlineColourId,    juce::Colours::transparentBlack);

    auto tabBg = Colors::bgElevated();
    rightTabs_->addTab(BM_TJ("live.tab.suggestions"), tabBg, tabContentSuggestions_.get(), false);
    rightTabs_->addTab(BM_TJ("live.tab.trending"),    tabBg, tabContentTrending_.get(),    false);
    rightTabs_->addTab(BM_TJ("live.tab.rediscover"),  tabBg, tabContentRediscover_.get(),  false);
    rightTabs_->addTab(BM_TJ("live.tab.history"),     tabBg, tabContentHistory_.get(),     false);
    rightTabs_->addTab(BM_TJ("live.tab.myStyle"),     tabBg, tabContentMyStyle_.get(),     false);
    addAndMakeVisible(*rightTabs_);

    if (suggestionList_) tabContentSuggestions_->addAndMakeVisible(*suggestionList_);
    if (profileCb_)        tabContentSuggestions_->addAndMakeVisible(*profileCb_);
    if (profileSaveAsBtn_) tabContentSuggestions_->addAndMakeVisible(*profileSaveAsBtn_);
    if (profileLbl_)       profileLbl_->setVisible(false);
    if (smartPanel_)     tabContentSuggestions_->addChildComponent(*smartPanel_);
    if (queuePanel_)     tabContentSuggestions_->addAndMakeVisible(*queuePanel_);
    if (trendingList_)       tabContentTrending_->addAndMakeVisible(*trendingList_);
    if (genreFilter_)        tabContentTrending_->addAndMakeVisible(*genreFilter_);
    if (countryFilter_)      tabContentTrending_->addAndMakeVisible(*countryFilter_);
    if (trendingSortCb_)     tabContentTrending_->addAndMakeVisible(*trendingSortCb_);
    if (trendingRefreshBtn_) tabContentTrending_->addAndMakeVisible(*trendingRefreshBtn_);
    if (trendingDateLbl_)    tabContentTrending_->addAndMakeVisible(*trendingDateLbl_);
    if (historyList_)      tabContentHistory_    ->addAndMakeVisible(*historyList_);
    if (exportSessionBtn_) tabContentHistory_    ->addAndMakeVisible(*exportSessionBtn_);
    if (rediscoverPanel_) tabContentRediscover_->addAndMakeVisible(*rediscoverPanel_);

    rightTabs_->setCurrentTabIndex(0);

    {
        const int srcId = Prefs::getInt("live.sourceSelectorId", 0);
        if (sourceSelector_ && srcId >= 1 && srcId <= sourceSelector_->getNumItems())
            sourceSelector_->setSelectedId(srcId, juce::sendNotificationSync);
        alwaysOnTop_ = Prefs::getBool("live.alwaysOnTop", false);
        if (alwaysOnTopToggle_)
            alwaysOnTopToggle_->setToggleState(alwaysOnTop_, juce::dontSendNotification);
        if (alwaysOnTop_) {
            if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
                w->setAlwaysOnTop(true);
        }
        pollingIntervalSec_ = Prefs::getInt("live.pollingIntervalSec", pollingIntervalSec_);
        const int tgId = Prefs::getInt("live.trendingGenreId", 0);
        if (genreFilter_ && tgId >= 1 && tgId <= genreFilter_->getNumItems())
            genreFilter_->setSelectedId(tgId, juce::dontSendNotification);
        const int tcId = Prefs::getInt("live.trendingCountryId", 0);
        if (countryFilter_ && tcId >= 1 && tcId <= countryFilter_->getNumItems())
            countryFilter_->setSelectedId(tcId, juce::dontSendNotification);
    }

    switchAIMode();

    cabinMode_ = Prefs::getBool("live.cabinMode", false);
    applyCabinMode();
    loadChartIndexAsync();

    startTimerHz(15);
}


int BeatMateLiveView::measureButtonWidth(const juce::String& label, int minWidth,
                                          int padding, float fontHeight) const
{
    juce::Font f(juce::FontOptions{}.withHeight(fontHeight).withStyle("Bold"));
    const int w = f.getStringWidth(label) + padding;
    return juce::jmax(minWidth, w);
}

void BeatMateLiveView::handleConnectClicked()
{
    startMonitoring();
    if (connectButton_)    connectButton_->setVisible(false);
    if (disconnectButton_) disconnectButton_->setVisible(true);
    resized();
    repaint();

    // Immediate first poll — don't wait for the timer's next tick (~2s).
    if (currentSource_ == "AUDIO") {
        if (auto* pool = ::BeatMate::getBackgroundPool()) {
            pool->addJob([this]() {
                try { pollCurrentTrack(); }
                catch (const std::exception& e) {
                    spdlog::error("[Live] first pollCurrentTrack failed: {}", e.what());
                }
            });
        } else {
            pollCurrentTrack();
        }
    } else {
        pollCurrentTrack();
    }

    juce::Component::SafePointer<juce::Component> safe { this };
    juce::Timer::callAfterDelay(15000, [safe]() {
        auto* self = dynamic_cast<BeatMateLiveView*>(safe.getComponent());
        if (!self) return;
        if (!self->nowPlaying_.connected) {
            self->stopMonitoring();
            if (self->connectButton_)    self->connectButton_->setVisible(true);
            if (self->disconnectButton_) self->disconnectButton_->setVisible(false);
            self->resized();
            self->repaint();
        }
    });
}

void BeatMateLiveView::handleDisconnectClicked()
{
    stopMonitoring();
    nowPlaying_.connected = false;
    if (connectButton_)    connectButton_->setVisible(true);
    if (disconnectButton_) disconnectButton_->setVisible(false);
    resized();
    repaint();
}

void BeatMateLiveView::rebuildProfileCombo()
{
    if (!profileCb_) return;
    extern BeatMate::ServiceLocator* g_serviceLocator;
    if (!g_serviceLocator) return;
    auto* svc = g_serviceLocator->tryGet<Services::Suggestions::DJProfileService>();
    if (!svc) return;

    const juce::String previous = profileCb_->getText();
    profileCb_->clear(juce::dontSendNotification);
    profileCb_->addItem(BM_TJ("live.noProfile"), 1);

    int id = 2;
    auto profiles = svc->listProfiles();
    int matchId = 1;
    for (const auto& p : profiles) {
        profileCb_->addItem(juce::String(p.name), id);
        if (juce::String(p.name) == previous
            || (activeProfileName_.empty() == false && p.name == activeProfileName_))
            matchId = id;
        ++id;
    }
    profileCb_->setSelectedId(matchId, juce::dontSendNotification);
}

void BeatMateLiveView::onProfileSelected()
{
    if (!profileCb_ || !smartEngine_) return;
    const int sel = profileCb_->getSelectedId();
    if (sel <= 1) { activeProfileName_.clear(); return; }

    extern BeatMate::ServiceLocator* g_serviceLocator;
    if (!g_serviceLocator) return;
    auto* svc = g_serviceLocator->tryGet<Services::Suggestions::DJProfileService>();
    if (!svc) return;

    const std::string name = profileCb_->getText().toStdString();
    auto opt = svc->loadProfile(name);
    if (!opt) return;

    svc->applyProfile(*opt, *smartEngine_);
    activeProfileName_ = name;
    if (smartPanel_) {
        smartPanel_->setProfileContext(svc, activeProfileName_);
        smartPanel_->refresh();
    }
}

void BeatMateLiveView::onSaveProfileAs()
{
    if (!smartEngine_) return;
    extern BeatMate::ServiceLocator* g_serviceLocator;
    if (!g_serviceLocator) return;
    auto* svc = g_serviceLocator->tryGet<Services::Suggestions::DJProfileService>();
    if (!svc) return;

    auto* aw = new juce::AlertWindow(BM_TJ("live.newProfile"),
        BM_TJ("live.profileName"), juce::MessageBoxIconType::QuestionIcon);
    aw->addTextEditor("name", BM_TJ("live.myProfile"));
    aw->addButton(BM_TJ("live.save"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton(BM_TJ("live.cancel"),     0, juce::KeyPress(juce::KeyPress::escapeKey));

    aw->enterModalState(true,
        juce::ModalCallbackFunction::create([this, aw, svc](int result) {
            std::unique_ptr<juce::AlertWindow> own(aw);
            if (result != 1) return;
            const auto name = aw->getTextEditorContents("name").trim();
            if (name.isEmpty()) return;

            Models::DJProfile p;
            if (!activeProfileName_.empty()) {
                if (auto base = svc->loadProfile(activeProfileName_)) p = *base;
            }
            p.name = name.toStdString();
            if (p.venue.empty()) p.venue = "Custom";
            if (svc->saveProfile(p)) {
                activeProfileName_ = p.name;
                rebuildProfileCombo();
            }
        }), false);
}

void BeatMateLiveView::refreshSmartSuggestFor(int64_t trackId)
{
    if (!smartEngine_ || !smartPanel_) return;
    smartEngine_->setCurrentTrack(trackId);
    smartEngine_->invalidateCache();

    juce::Component::SafePointer<BeatMateLiveView> safe(this);
    auto apply = [safe]()
    {
        auto* self = safe.getComponent();
        if (!self || !self->smartPanel_) return;
        self->updatePhraseHintCache();
        self->switchAIMode();
        if (!self->smartPanel_->isVisible())
            self->smartPanel_->refresh();

        if (self->rediscoverPanel_)
            self->rediscoverPanel_->onCurrentTrackChanged();
    };

    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating();
        mm != nullptr && mm->isThisTheMessageThread())
        apply();
    else
        juce::MessageManager::callAsync(apply);
}

BeatMateLiveView::~BeatMateLiveView()
{
    stopTimer();

    if (nowPlayingSvc_) {
        nowPlayingSvc_->stop();
        nowPlayingSvc_->onTrackChanged = nullptr;
    }

    // Detach from UnifiedDJHistory so its polling thread cannot invoke
    extern BeatMate::ServiceLocator* g_serviceLocator;
    if (g_serviceLocator) {
        if (auto* uhist = g_serviceLocator->tryGet<Services::DJSoftware::UnifiedDJHistory>()) {
            uhist->onNewPlay = nullptr;
        }
        if (auto* listener = g_serviceLocator->tryGet<Services::Audio::AudioListenerService>()) {
            listener->setOnDetection(nullptr);
        }
    }
}


void BeatMateLiveView::generateWaveformData()
{
    if (!nowPlayingPath_.isEmpty()) {
        Services::Library::PeakFileService svc;
        Services::Library::PeakConfig cfg;
        cfg.cacheDirectory = juce::File::getSpecialLocation(
            juce::File::userApplicationDataDirectory)
                .getChildFile("BeatMate").getChildFile("Peaks")
                .getFullPathName().toStdString();
        cfg.useCache = true;
        svc.initialize(cfg);
        auto peaks = svc.getPeaksByPath(nowPlayingPath_.toStdString());
        if (peaks && peaks->isValid() && peaks->segmentCount > 0) {
            const int n = peaks->segmentCount;
            waveformLow_.assign(peaks->lowFreq.begin(),
                                peaks->lowFreq.begin() + n);
            waveformMid_.assign(peaks->midFreq.begin(),
                                peaks->midFreq.begin() + n);
            waveformHigh_.assign(peaks->highFreq.begin(),
                                 peaks->highFreq.begin() + n);
            // Normalise to [0, 1] in case the cache stores raw amplitudes.
            for (auto& v : waveformLow_)  v = juce::jlimit(0.0f, 1.0f, v);
            for (auto& v : waveformMid_)  v = juce::jlimit(0.0f, 1.0f, v);
            for (auto& v : waveformHigh_) v = juce::jlimit(0.0f, 1.0f, v);
            return;
        }
    }

    if (!nowPlayingPath_.isEmpty()) {
        juce::File audio(nowPlayingPath_);
        juce::File anlz = audio.getParentDirectory().getChildFile("ANLZ0000.DAT");
        if (!anlz.existsAsFile())
            anlz = audio.withFileExtension(".ANLZ.DAT");
        if (!anlz.existsAsFile())
            anlz = audio.withFileExtension(".DAT");
        if (anlz.existsAsFile()) {
            Services::Rekordbox::AnlzWaveform wf;
            if (Services::Rekordbox::RekordboxAnlzParser::parseFile(
                    anlz.getFullPathName().toStdString(),
                    nullptr, nullptr, &wf) && !wf.columns.empty()) {
                const size_t n = wf.columns.size();
                waveformLow_.resize(n);
                waveformMid_.resize(n);
                waveformHigh_.resize(n);
                for (size_t i = 0; i < n; ++i) {
                    waveformLow_[i]  = wf.columns[i].heightBass / 31.0f;
                    waveformMid_[i]  = wf.columns[i].heightMid  / 31.0f;
                    waveformHigh_[i] = wf.columns[i].heightHigh / 31.0f;
                }
                spdlog::info("[Live] Real waveform loaded from ANLZ: {} columns", n);
                return;
            }
        }
    }

    waveformLow_.assign(200, 0.0f);
    waveformMid_.assign(200, 0.0f);
    waveformHigh_.assign(200, 0.0f);
    spdlog::info("[Live] No peaks / no ANLZ for current track — waveform blank (demo mode)");
}



void BeatMateLiveView::timerCallback()
{
    playheadPosition_ += 0.003f;
    if (playheadPosition_ > 1.0f)
        playheadPosition_ = 0.0f;

    waveformAnimOffset_++;

    if (waveformAnimOffset_ % 8 == 0)
        ledBlinkState_ = !ledBlinkState_;

    pollCounter_++;
    const juce::int64 nowMs = juce::Time::currentTimeMillis();
    const juce::int64 sinceActivityMs = (lastDetectedActivityMs_ > 0)
        ? nowMs - lastDetectedActivityMs_
        : std::numeric_limits<juce::int64>::max();

    int pollFrames = pollingIntervalSec_ * 15; // user-configured default (~2s)
    if (sinceActivityMs < kStaleThresholdMs) {
        pollFrames = 7;
    } else if (sinceActivityMs > kIdleThresholdMs) {
        pollFrames = 75;
    }
    if (pollFrames < 7) pollFrames = 7;
    if (pollCounter_ % pollFrames == 0) {
        if (currentSource_ == "AUDIO") {
            if (!pollBusy_->exchange(true)) {
                auto busy = pollBusy_;
                auto runPoll = [this, busy]() {
                    try { pollCurrentTrack(); }
                    catch (const std::exception& e) {
                        spdlog::error("[Live] pollCurrentTrack failed: {}", e.what());
                    }
                    catch (...) {}
                    juce::MessageManager::callAsync([busy]() { busy->store(false); });
                };
                if (auto* pool = ::BeatMate::getBackgroundPool())
                    pool->addJob(runPoll);
                else
                    runPoll();
            }
        } else {
            pollCurrentTrack();
        }
    }

    const bool connectedNow = nowPlaying_.connected;
    const bool discVis = disconnectButton_ && disconnectButton_->isVisible();
    if (!connectedNow && discVis) {
        if (connectButton_)    connectButton_->setVisible(true);
        if (disconnectButton_) disconnectButton_->setVisible(false);
        monitoring_ = false;
        resized();
    } else if (connectedNow && connectButton_ && connectButton_->isVisible()) {
        if (connectButton_)    connectButton_->setVisible(false);
        if (disconnectButton_) disconnectButton_->setVisible(true);
        resized();
    }

    auto area = getLocalBounds().reduced(12);
    auto nowPlayingArea = area.withTrimmedTop(58).withHeight(240);
    repaint(nowPlayingArea);
}

void BeatMateLiveView::startMonitoring()
{
    monitoring_ = true;
    nowPlaying_.connected = false;
    if (!isTimerRunning())
        startTimerHz(15);
    repaint();
}

void BeatMateLiveView::stopMonitoring()
{
    monitoring_ = false;
    nowPlaying_.connected = false;
    repaint();
}

void BeatMateLiveView::pollCurrentTrack()
{
    if (currentSource_ == "AUDIO")
    {
        pollAudioIa();
        if (nowPlaying_.connected) activeDetectedSource_ = "AUDIO";
        return;
    }

    if (!nowPlayingSvc_) return;
    nowPlayingSvc_->pollNow();
    const auto detected = nowPlayingSvc_->getCurrent();
    const juce::int64 nowMs = juce::Time::currentTimeMillis();
    const bool freshTrack = detected.has_value()
        && (nowMs - detected->updatedAtMs) < 5 * 60 * 1000;
    nowPlaying_.connected = nowPlayingSvc_->isAnySoftwareRunning() || freshTrack;
    if (detected.has_value() && juce::String(detected->title) != lastDetectedTitle_)
        applyDetectedTrack(*detected);
}

void BeatMateLiveView::applyDetectedTrack(const Services::Live::NowPlayingTrack& detected)
{
    const juce::String title  = juce::String(detected.title).trim();
    const juce::String artist = juce::String(detected.artist).trim();
    if (title.isEmpty()) return;

    nowPlaying_.connected = true;
    activeDetectedSource_ = juce::String(detected.source);
    if (title == lastDetectedTitle_) return;

    lastDetectedTitle_ = title;
    lastDetectedActivityMs_ = juce::Time::currentTimeMillis();
    nowPlaying_.title  = title;
    nowPlaying_.artist = artist;
    nowPlaying_.deck   = activeDetectedSource_.toUpperCase();
    nowPlaying_.bpm    = detected.bpm;
    nowPlaying_.key    = juce::String(detected.key);
    nowPlaying_.energy = 0.0f;
    nowPlayingPath_    = juce::String(detected.filePath);

    Models::Track lib;
    bool inLibrary = false;
    extern BeatMate::ServiceLocator* g_serviceLocator;
    if (!detected.filePath.empty() && g_serviceLocator) {
        if (auto* db = g_serviceLocator->tryGet<Services::Library::TrackDatabase>()) {
            try {
                if (auto byPath = db->getTrackByPath(detected.filePath);
                    byPath.has_value() && byPath->id > 0) {
                    lib = *byPath;
                    inLibrary = true;
                }
            } catch (...) {}
        }
    }
    if (!inLibrary && m_provider) {
        using Services::Streaming::ChartsService;
        const auto wanted = ChartsService::matchKey(detected.title, detected.artist);
        auto pick = [&](const std::vector<Models::Track>& results) {
            for (const auto& r : results) {
                if (ChartsService::matchKey(r.title, r.artist) == wanted) {
                    lib = r;
                    inLibrary = true;
                    return true;
                }
            }
            return false;
        };
        const auto byTitle = m_provider->searchTracks(title.toStdString());
        if (!pick(byTitle) && artist.isNotEmpty())
            pick(m_provider->searchTracks((artist + " " + title).toStdString()));

        // Relaxed pass: the DJ software often spells the artist differently
        if (!inLibrary && artist.isNotEmpty()) {
            const auto wantedTitle  = ChartsService::normalizeKey(title.toStdString());
            const auto wantedArtist = ChartsService::normalizeKey(artist.toStdString());
            auto commonPrefix = [](const std::string& a, const std::string& b) {
                size_t n = std::min(a.size(), b.size()), i = 0;
                while (i < n && a[i] == b[i]) ++i;
                return i;
            };
            const Models::Track* best = nullptr;
            size_t bestPrefix = 0;
            for (const auto& r : byTitle) {
                if (ChartsService::normalizeKey(r.title) != wantedTitle) continue;
                const auto artistNorm = ChartsService::normalizeKey(r.artist);
                if (artistNorm.empty() || wantedArtist.empty()) continue;
                const size_t p = commonPrefix(artistNorm, wantedArtist);
                const size_t shorter = std::min(artistNorm.size(), wantedArtist.size());
                if (p >= std::max<size_t>(3, (shorter * 7) / 10) && p > bestPrefix) {
                    best = &r;
                    bestPrefix = p;
                }
            }
            if (best) {
                lib = *best;
                inLibrary = true;
                spdlog::info("[Live] Relaxed match: '{}' - '{}' -> library id={} ('{}' - '{}')",
                             title.toStdString(), artist.toStdString(),
                             lib.id, lib.title, lib.artist);
            }
        }

        if (!inLibrary && artist.isEmpty() && !byTitle.empty()) {
            lib = byTitle.front();
            inLibrary = true;
        }
    }

    Models::Track seed;
    if (inLibrary) {
        seed = lib;
        if (lib.bpm > 0.0) nowPlaying_.bpm = lib.bpm;
        if (!lib.camelotKey.empty())  nowPlaying_.key = juce::String(lib.camelotKey);
        else if (!lib.key.empty())    nowPlaying_.key = juce::String(lib.key);
        nowPlaying_.energy = lib.energy;
        if (nowPlayingPath_.isEmpty()) nowPlayingPath_ = juce::String(lib.filePath);
    } else {
        seed.title    = title.toStdString();
        seed.artist   = artist.toStdString();
        seed.bpm      = detected.bpm;
        seed.key      = detected.key;
        seed.filePath = detected.filePath;
    }

    addToHistory(title, artist, nowPlaying_.bpm, nowPlaying_.key, nowPlaying_.energy);
    generateWaveformData();

    const bool engineSeeded = inLibrary && lib.id > 0 && smartEngine_ != nullptr;

    if (m_provider && !engineSeeded) {
        auto compatible = m_provider->getSuggestionsFor(seed, 50);
        suggestions_.clear();
        for (const auto& s : compatible) {
            SuggestionEntry e;
            e.title  = juce::String(s.title);
            e.artist = juce::String(s.artist);
            e.bpm    = s.bpm;
            e.key    = juce::String(s.key.empty() ? s.camelotKey : s.key);
            e.score  = 0.7f;
            e.relevance = e.score;
            e.streamingSource = (s.source == Models::TrackSource::Streaming)
                ? "streaming" : "local";
            e.filePath = juce::String(s.filePath);
            e.energy = s.energy;
            suggestions_.push_back(e);
        }
        updateSuggestions();
    }

    if (engineSeeded) {
        smartEngine_->noteJustPlayed(lib);
        refreshSmartSuggestFor(lib.id);
    } else if (smartPanel_) {
        smartPanel_->refresh();
    }

    monitoring_ = true;
    if (!isTimerRunning()) startTimerHz(15);
    repaint();
}

void BeatMateLiveView::pollAudioIa()
{
    extern BeatMate::ServiceLocator* g_serviceLocator;
    if (!g_serviceLocator) return;
    auto* listener = g_serviceLocator->tryGet<Services::Audio::AudioListenerService>();
    if (!listener) return;
    if (!listener->isRunning()) {
        if (m_provider) listener->setProvider(m_provider);
        listener->setOnDetection([safeThis = juce::Component::SafePointer<BeatMateLiveView>(this)](const Services::Audio::AudioListenerService::Detection& d) {
            juce::MessageManager::callAsync([safeThis, d]() {
                auto* self = safeThis.getComponent();
                if (!self) return;
                self->nowPlaying_.connected = true;
                self->nowPlaying_.title  = juce::String(d.title);
                self->nowPlaying_.artist = juce::String(d.artist);
                if (self->lastDetectedTitle_ != self->nowPlaying_.title) {
                    self->lastDetectedTitle_ = self->nowPlaying_.title;
                    self->addToHistory(self->nowPlaying_.title, self->nowPlaying_.artist,
                                       self->nowPlaying_.bpm, self->nowPlaying_.key, self->nowPlaying_.energy);
                    if (self->m_provider) {
                        auto results = self->m_provider->searchTracks(self->nowPlaying_.title.toStdString());
                        if (!results.empty()) {
                            if (results[0].id > 0 && self->smartEngine_) {
                                self->smartEngine_->noteJustPlayed(results[0]);
                                self->refreshSmartSuggestFor(results[0].id);
                            } else {
                                auto compatible = self->m_provider->getSuggestionsFor(results[0], 50);
                                self->suggestions_.clear();
                                for (const auto& s : compatible) {
                                    SuggestionEntry e;
                                    e.title    = juce::String(s.title);
                                    e.artist   = juce::String(s.artist);
                                    e.bpm      = s.bpm;
                                    e.key      = juce::String(s.key.empty() ? s.camelotKey : s.key);
                                    e.score    = 0.7f;
                                    e.relevance = e.score;
                                    e.filePath = juce::String(s.filePath);
                                    e.energy   = s.energy;
                                    self->suggestions_.push_back(e);
                                }
                                self->updateSuggestions();
                            }
                        }
                    }
                }
                self->repaint();
            });
        });
        listener->start();
    }
    auto st = listener->getStatus();
    nowPlaying_.connected = st.deviceOpen;
}

void BeatMateLiveView::updateSuggestions()
{
    using Services::Streaming::ChartsService;
    std::set<std::string> played;
    for (const auto& h : history_)
        played.insert(ChartsService::matchKey(h.title.toStdString(), h.artist.toStdString()));

    for (auto& s : suggestions_)
    {
        if (s.streamingSource.isEmpty() && s.filePath.isNotEmpty())
            s.streamingSource = "local";
        if (s.filePath.isNotEmpty() && s.streamingSource == "local")
            s.inLibrary = true;

        const auto k = ChartsService::matchKey(s.title.toStdString(), s.artist.toStdString());
        s.chartPos = 0; s.chartDelta = 0; s.chartNew = false;
        if (auto it = chartIndex_.find(k); it != chartIndex_.end())
        {
            s.chartPos   = it->second.position;
            s.chartDelta = it->second.delta;
            s.chartNew   = it->second.isNew;
        }
        s.alreadyPlayed = played.count(k) > 0;

        if (s.reason.isEmpty())
            s.reason = s.baseReason.isNotEmpty()
                ? s.baseReason
                : juce::String::fromUTF8("BPM/cl\xc3\xa9 compatibles");
    }

    suggestionList_->updateContent();
    suggestionList_->repaint();
    if (tabContentSuggestions_) tabContentSuggestions_->repaint();
    updateQueueHeader();
}

void BeatMateLiveView::rebuildEntriesFromEngine()
{
    if (!smartEngine_ || smartEngine_->getCurrentTrack() <= 0)
    {
        updateSuggestions();
        return;
    }

    auto* engine = smartEngine_;
    juce::Component::SafePointer<BeatMateLiveView> safe(this);
    auto job = [safe, engine]()
    {
        auto results = std::make_shared<std::vector<Services::Suggestions::Suggestion>>();
        try { *results = engine->getSuggestions(30); } catch (...) {}
        juce::MessageManager::callAsync([safe, results]()
        {
            auto* self = safe.getComponent();
            if (!self) return;

            std::vector<SuggestionEntry> streaming;
            for (auto& e : self->suggestions_)
                if (!e.inLibrary && e.streamingSource.isNotEmpty()
                    && e.streamingSource != "local")
                    streaming.push_back(std::move(e));

            self->suggestions_.clear();
            for (const auto& s : *results)
            {
                SuggestionEntry e;
                e.title    = juce::String::fromUTF8(s.title.c_str());
                e.artist   = juce::String::fromUTF8(s.artist.c_str());
                e.bpm      = s.bpm;
                e.key      = juce::String::fromUTF8(s.camelotKey.c_str());
                e.score    = juce::jlimit(0.0f, 1.0f,
                                          static_cast<float>(s.totalScore) / 100.0f);
                e.relevance = e.score;
                e.streamingSource = "local";
                e.inLibrary = true;
                e.filePath = juce::String::fromUTF8(s.filePath.c_str());
                e.energy   = static_cast<float>(s.energyScore);
                e.reason   = juce::String::fromUTF8(s.reason.c_str());
                e.baseReason = e.reason;
                self->suggestions_.push_back(std::move(e));
            }
            for (auto& e : streaming)
                self->suggestions_.push_back(std::move(e));

            self->updateSuggestions();
        });
    };

    if (auto* pool = ::BeatMate::getBackgroundPool())
        pool->addJob(job);
    else
        job();
}

void BeatMateLiveView::applyModeWeights()
{
    if (!smartEngine_) return;
    smartEngine_->setWeights(std::array<double, 10>{
        0.24, 0.26, 0.11, 0.10, 0.02, 0.03, 0.13, 0.01, 0.01, 0.09 });
}

void BeatMateLiveView::loadChartIndexAsync()
{
    juce::String cc = selectedTrendingCountry();
    if (cc.isEmpty()) cc = "fr";
    const std::string country = cc.toLowerCase().toStdString();
    juce::Component::SafePointer<BeatMateLiveView> safe(this);

    auto job = [safe, country]()
    {
        using Services::Streaming::ChartsService;
        auto chart = ChartsService::instance().getCachedChart(country);
        auto idx = std::make_shared<std::map<std::string, ChartHit>>();
        for (const auto& e : chart.entries)
            (*idx)[ChartsService::matchKey(e.title, e.artist)] = ChartHit { e.position, e.delta, e.isNew };
        juce::MessageManager::callAsync([safe, idx]()
        {
            auto* self = safe.getComponent();
            if (!self) return;
            self->chartIndex_ = std::move(*idx);
            if (!self->suggestions_.empty()) self->updateSuggestions();
        });
    };

    if (auto* pool = ::BeatMate::getBackgroundPool())
        pool->addJob(job);
    else
        job();
}

void BeatMateLiveView::applyCabinMode()
{
    if (suggestionModel_) suggestionModel_->cabin = cabinMode_;
    if (suggestionList_)  suggestionList_->setRowHeight(cabinMode_ ? 88 : 48);
    if (cabinMode_)
    {
        if (smartPanel_)     smartPanel_->setVisible(false);
        if (suggestionList_) suggestionList_->setVisible(true);
        if (rightTabs_)      rightTabs_->setCurrentTabIndex(0);
    }
    else
    {
        switchAIMode();
    }
    if (suggestionList_)
    {
        suggestionList_->updateContent();
        suggestionList_->repaint();
    }
    if (tabContentSuggestions_) tabContentSuggestions_->resized();
    resized();
    repaint();
}

void BeatMateLiveView::toggleCabinMode()
{
    cabinMode_ = !cabinMode_;
    Prefs::setBool("live.cabinMode", cabinMode_);
    applyCabinMode();
}

void BeatMateLiveView::addEntryToQueue(const SuggestionEntry& entry)
{
    using Services::Streaming::ChartsService;
    const auto k = ChartsService::matchKey(entry.title.toStdString(), entry.artist.toStdString());
    for (const auto& q : setQueue_)
        if (ChartsService::matchKey(q.title.toStdString(), q.artist.toStdString()) == k)
            return;
    setQueue_.push_back(entry);
    persistSetQueue();
    updateQueueHeader();
    if (queuePanel_)
    {
        queuePanel_->refreshList();
        queuePanel_->resized();
    }
    if (tabContentSuggestions_) tabContentSuggestions_->resized();
}

void BeatMateLiveView::persistSetQueue()
{
    juce::Array<juce::var> arr;
    for (const auto& q : setQueue_)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("title",      q.title);
        o->setProperty("artist",     q.artist);
        o->setProperty("filePath",   q.filePath);
        o->setProperty("bpm",        q.bpm);
        o->setProperty("key",        q.key);
        o->setProperty("previewUrl", q.previewUrl);
        o->setProperty("inLibrary",  q.inLibrary);
        o->setProperty("energy",     static_cast<double>(q.energy));
        arr.add(juce::var(o));
    }
    Prefs::setString("live.setQueue",
                     juce::JSON::toString(juce::var(arr), true).toStdString());
}

void BeatMateLiveView::restoreSetQueue()
{
    setQueue_.clear();
    const auto raw = juce::String(Prefs::getString("live.setQueue"));
    if (raw.isEmpty()) return;
    auto parsed = juce::JSON::parse(raw);
    if (auto* arr = parsed.getArray())
    {
        for (const auto& v : *arr)
        {
            if (auto* o = v.getDynamicObject())
            {
                SuggestionEntry q;
                q.title      = o->getProperty("title").toString();
                q.artist     = o->getProperty("artist").toString();
                q.filePath   = o->getProperty("filePath").toString();
                q.bpm        = static_cast<double>(o->getProperty("bpm"));
                q.key        = o->getProperty("key").toString();
                q.previewUrl = o->getProperty("previewUrl").toString();
                q.inLibrary  = static_cast<bool>(o->getProperty("inLibrary"));
                q.energy     = static_cast<float>(static_cast<double>(o->getProperty("energy")));
                if (q.title.isNotEmpty()) setQueue_.push_back(std::move(q));
            }
        }
    }
    if (queuePanel_) queuePanel_->refreshList();
    updateQueueHeader();
}

void BeatMateLiveView::updateQueueHeader()
{
    if (!queuePanel_) return;
    juce::String txt;
    if (!setQueue_.empty())
    {
        const auto& head = setQueue_.front();
        juce::StringArray parts;
        const auto rel = camelotRelation(nowPlaying_.key, head.key);
        if (rel.known) parts.add(rel.label);
        const auto bf = bpmFit(nowPlaying_.bpm, head.bpm);
        if (bf.known) parts.add(bf.label);
        if (!parts.isEmpty())
        {
            const float compat = juce::jlimit(0.0f, 1.0f,
                (rel.known ? rel.score : 0.5f) * 0.55f + (bf.known ? bf.score : 0.5f) * 0.45f);
            txt = "Prochain : " + juce::String(static_cast<int>(compat * 100.0f)) + "% "
                + juce::String::fromUTF8("\xc2\xb7 ") + parts.joinIntoString(juce::String::fromUTF8(" \xc2\xb7 "));
        }
        else
        {
            txt = "Prochain : " + head.title;
        }
    }
    queuePanel_->setHeadLine(txt);
}

void BeatMateLiveView::playEntryPreview(const SuggestionEntry& entry, const juce::String& rowPrefix)
{
    juce::String url;
    if (entry.filePath.isNotEmpty() && juce::File::isAbsolutePath(entry.filePath)
        && juce::File(entry.filePath).existsAsFile())
        url = entry.filePath;
    else if (entry.previewUrl.isNotEmpty())
        url = entry.previewUrl;
    Widgets::LivePreviewPlayer::getInstance().playPreview(
        (rowPrefix + "|" + entry.artist + "|" + entry.title).toStdString(),
        entry.artist.toStdString(), entry.title.toStdString(),
        url.toStdString());
}

void BeatMateLiveView::addToHistory(const juce::String& title, const juce::String& artist,
                                     double bpm, const juce::String& key, float energy)
{
    auto now = juce::Time::getCurrentTime();
    juce::String ts = now.formatted("%H:%M");

    history_.insert(history_.begin(), { title, artist, bpm, key, energy, ts, "0:00" });
    totalTracksPlayed_ = static_cast<int>(history_.size());
    updateStats();
    historyList_->updateContent();
    if (tabContentHistory_) tabContentHistory_->repaint();
    if (!seedingHistory_)
    {
        refreshSimilarStreaming(title, artist);
        updateQueueHeader();
    }
}

void BeatMateLiveView::updateStats()
{
    if (history_.empty())
    {
        averageBPM_ = 0.0;
        return;
    }
    double sum = 0.0;
    for (const auto& h : history_)
        sum += h.bpm;
    averageBPM_ = sum / static_cast<double>(history_.size());
}

void BeatMateLiveView::switchAIMode()
{
    applyModeWeights();
    if (smartEngine_) smartEngine_->invalidateCache();

    if (smartEngine_ && smartEngine_->getCurrentTrack() <= 0)
    {
        int64_t anchor = 0;
        if (m_provider)
        {
            auto recent = m_provider->getRecentlyPlayed(1);
            if (!recent.empty() && recent[0].id > 0)
                anchor = recent[0].id;
        }
        if (anchor <= 0)
        {
            extern BeatMate::ServiceLocator* g_serviceLocator;
            if (g_serviceLocator)
                if (auto* db = g_serviceLocator->tryGet<Services::Library::TrackDatabase>())
                {
                    auto cand = db->getTracksByQuery(
                        "SELECT * FROM tracks ORDER BY date_added DESC LIMIT 40");
                    for (const auto& t : cand)
                        if (t.id > 0 && t.bpm > 0.0
                            && (!t.camelotKey.empty() || !t.key.empty()))
                        { anchor = t.id; break; }
                }
        }
        if (anchor > 0)
            smartEngine_->setCurrentTrack(anchor);
    }

    const bool showSmart = (!cabinMode_
                            && smartEngine_ != nullptr && smartPanel_ != nullptr);
    if (smartPanel_)
        smartPanel_->setVisible(showSmart);
    if (suggestionList_ && !compactMode_)
        suggestionList_->setVisible(!showSmart);
    if (tabContentSuggestions_)
        tabContentSuggestions_->resized();
    if (showSmart)
        smartPanel_->refresh();

    rebuildEntriesFromEngine();

    repaint();
}

juce::String BeatMateLiveView::selectedTrendingCountry() const
{
    static const char* const kCC[] = {
        "", "", "fr", "us", "gb", "de", "jp", "br", "es", "it", "global"
    };
    const int id = countryFilter_ ? countryFilter_->getSelectedId() : 1;
    if (id >= 2 && id <= 10) return kCC[id];
    return {};
}

void BeatMateLiveView::reloadTrendingSelection(bool forceRefresh)
{
    const auto cc = selectedTrendingCountry();
    if (cc.isEmpty()) loadTrendingFromCollection();
    else              loadTrendingForCountry(cc, forceRefresh);
}

void BeatMateLiveView::applyTrendingSort()
{
    if (trending_.size() < 2) return;
    if (trending_.front().position <= 0) return;
    const bool momentum = trendingSortCb_ && trendingSortCb_->getSelectedId() == 2;
    auto momentumOf = [](const TrendingTrack& t) {
        return t.isNew ? (101 - t.position) : t.delta;
    };
    if (momentum)
        std::stable_sort(trending_.begin(), trending_.end(),
            [&momentumOf](const TrendingTrack& a, const TrendingTrack& b) {
                const int ma = momentumOf(a), mb = momentumOf(b);
                if (ma != mb) return ma > mb;
                return a.position < b.position;
            });
    else
        std::stable_sort(trending_.begin(), trending_.end(),
            [](const TrendingTrack& a, const TrendingTrack& b) {
                return a.position < b.position;
            });
}

void BeatMateLiveView::loadTrendingForCountry(const juce::String& cc, bool forceRefresh)
{
    trending_.clear();
    TrendingTrack loader;
    loader.position = 0;
    loader.title = juce::String("Chargement du classement ")
        + (cc == "global" ? juce::String("MONDE") : cc.toUpperCase());
    loader.artist = "";
    trending_.push_back(loader);
    if (trendingList_) {
        trendingList_->updateContent();
        trendingList_->repaint();
    }
    if (trendingDateLbl_)
        trendingDateLbl_->setText(
            juce::String::fromUTF8("R\xc3\xa9""cup\xc3\xa9ration des charts\xe2\x80\xa6"),
            juce::dontSendNotification);

    const juce::String country = cc.toLowerCase();
    juce::WeakReference<juce::Component> weakSelf { this };
    auto* provider = m_provider;

    auto job = [weakSelf, country, provider, forceRefresh]()
    {
        using BeatMate::Services::Streaming::ChartsService;
        auto chart = ChartsService::instance().getChart(country.toStdString(), forceRefresh);

        std::map<std::string, Models::Track> lib;
        if (provider != nullptr && !chart.entries.empty()) {
            try {
                for (auto& t : provider->getAllTracks())
                    lib.emplace(ChartsService::matchKey(t.title, t.artist), t);
            } catch (...) {}
        }

        auto entries = std::make_shared<std::vector<TrendingTrack>>();
        BeatMateLiveView::fillTrendingFromChart(chart, lib.empty() ? nullptr : &lib, *entries);

        const juce::String chartDate = juce::String(chart.chartDate);
        const juce::String source    = juce::String::fromUTF8(chart.source.c_str());
        const juce::String error     = juce::String::fromUTF8(chart.errorMessage.c_str());
        const bool fromCache = chart.fromCache;

        juce::MessageManager::callAsync(
            [weakSelf, entries, country, chartDate, source, error, fromCache]()
        {
            auto* self = dynamic_cast<BeatMateLiveView*>(weakSelf.get());
            if (!self) return;
            self->trending_.clear();
            if (entries->empty()) {
                TrendingTrack tt;
                tt.position = 0;
                tt.title = juce::String("Pas de donnees pour ")
                    + (country == "global" ? juce::String("MONDE") : country.toUpperCase());
                tt.artist = error.isNotEmpty() ? error
                                               : juce::String("Verifiez votre connexion");
                self->trending_.push_back(tt);
                if (self->trendingDateLbl_)
                    self->trendingDateLbl_->setText(error, juce::dontSendNotification);
            } else {
                self->trending_ = std::move(*entries);
                self->applyTrendingSort();
                if (self->trendingDateLbl_) {
                    juce::String d = chartDate;
                    if (d.length() >= 10)
                        d = d.substring(8, 10) + "/" + d.substring(5, 7)
                          + "/" + d.substring(0, 4);
                    juce::String txt = juce::String("Charts du ") + d
                        + juce::String::fromUTF8(" \xe2\x80\x94 ") + source;
                    if (fromCache) txt += " (cache)";
                    if (error.isNotEmpty())
                        txt += juce::String::fromUTF8(" \xc2\xb7 ") + error;
                    self->trendingDateLbl_->setText(txt, juce::dontSendNotification);
                }
            }
            self->trendingLoadedOnce_ = true;
            if (self->trendingList_) {
                self->trendingList_->updateContent();
                self->trendingList_->repaint();
            }
            if (self->tabContentTrending_) self->tabContentTrending_->repaint();
        });
    };

    if (auto* pool = ::BeatMate::getBackgroundPool())
        pool->addJob(job);
    else
        std::thread(job).detach();
}

void BeatMateLiveView::fillTrendingFromChart(
    const Services::Streaming::LiveChart& chart,
    const std::map<std::string, Models::Track>* library,
    std::vector<TrendingTrack>& out)
{
    using BeatMate::Services::Streaming::ChartsService;
    out.reserve(chart.entries.size());
    for (const auto& e : chart.entries) {
        BeatMateLiveView::TrendingTrack tt;
        tt.position     = e.position;
        tt.title        = juce::String::fromUTF8(e.title.c_str());
        tt.artist       = juce::String::fromUTF8(e.artist.c_str());
        tt.delta        = e.delta;
        tt.prevPosition = e.previousPosition;
        tt.isNew        = e.isNew;
        tt.source       = juce::String::fromUTF8(chart.source.c_str());
        tt.previewUrl   = juce::String(e.previewUrl);
        tt.trendScore   = e.position > 0
            ? juce::jmax(0.0f, 1.0f - static_cast<float>(e.position - 1) / 100.0f)
            : 0.0f;
        if (library != nullptr) {
            auto it = library->find(ChartsService::matchKey(e.title, e.artist));
            if (it != library->end()) {
                const auto& lt = it->second;
                tt.inLibrary = true;
                tt.localPath = juce::String(lt.filePath);
                tt.bpm       = lt.bpm;
                tt.key       = juce::String(lt.camelotKey.empty() ? lt.key : lt.camelotKey);
                tt.genre     = juce::String(lt.genre);
                tt.energy    = lt.energy;
            }
        }
        out.push_back(std::move(tt));
    }
}

void BeatMateLiveView::loadTrendingFromDiskCache()
{
    const auto cc = selectedTrendingCountry();
    if (cc.isEmpty()) return;
    auto chart = Services::Streaming::ChartsService::instance()
        .getCachedChart(cc.toLowerCase().toStdString());
    if (chart.entries.empty()) return;
    trending_.clear();
    fillTrendingFromChart(chart, nullptr, trending_);
    applyTrendingSort();
    if (trendingDateLbl_ && !chart.chartDate.empty()) {
        juce::String d = juce::String(chart.chartDate);
        if (d.length() >= 10)
            d = d.substring(8, 10) + "/" + d.substring(5, 7) + "/" + d.substring(0, 4);
        trendingDateLbl_->setText(
            juce::String("Charts du ") + d + juce::String::fromUTF8(" \xe2\x80\x94 ")
                + juce::String::fromUTF8(chart.source.c_str()) + " (cache)",
            juce::dontSendNotification);
    }
    if (trendingList_) {
        trendingList_->updateContent();
        trendingList_->repaint();
    }
    spdlog::info("[Charts] {} entrees chargees depuis le cache disque ({})",
                 trending_.size(), cc.toStdString());
}

void BeatMateLiveView::locateTrendingInLibrary(int row)
{
    if (row < 0 || row >= static_cast<int>(trending_.size()) || !m_provider) return;
    const auto entry = trending_[static_cast<size_t>(row)];
    auto results = m_provider->searchTracks(entry.title.toStdString());
    if (results.empty())
        results = m_provider->searchTracks(
            (entry.artist + " " + entry.title).toStdString());
    if (results.empty()) return;

    using Services::Streaming::ChartsService;
    const auto wanted = ChartsService::matchKey(entry.title.toStdString(),
                                                entry.artist.toStdString());
    const Models::Track* found = nullptr;
    for (const auto& t : results)
        if (ChartsService::matchKey(t.title, t.artist) == wanted) { found = &t; break; }
    if (!found) found = &results.front();

    if (found->id > 0)
    {
        refreshSmartSuggestFor(found->id);
    }
    else
    {
        auto compatible = m_provider->getSuggestionsFor(*found, 50);
        suggestions_.clear();
        for (const auto& s : compatible)
        {
            SuggestionEntry e;
            e.title  = juce::String(s.title);
            e.artist = juce::String(s.artist);
            e.bpm    = s.bpm;
            e.key    = juce::String(s.key.empty() ? s.camelotKey : s.key);
            e.score  = 0.7f;
            e.relevance = e.score;
            e.streamingSource = (s.source == Models::TrackSource::Streaming)
                ? "streaming" : "local";
            e.filePath = juce::String(s.filePath);
            e.energy = s.energy;
            suggestions_.push_back(e);
        }
        updateSuggestions();
    }
    refreshSimilarStreaming(entry.title, entry.artist);
    if (rightTabs_) rightTabs_->setCurrentTabIndex(0);
}

void BeatMateLiveView::refreshSimilarStreaming(const juce::String& title,
                                               const juce::String& artist)
{
    if (title.isEmpty() || title == "---" || artist.isEmpty() || artist == "---")
        return;
    using Services::Streaming::ChartsService;
    const juce::String key = juce::String(
        ChartsService::matchKey(title.toStdString(), artist.toStdString()));
    if (key == lastSimilarKey_)
    {
        const bool hasStreaming = std::any_of(suggestions_.begin(), suggestions_.end(),
            [](const SuggestionEntry& s) { return s.streamingSource == "deezer"; });
        if (hasStreaming) return;
    }
    lastSimilarKey_ = key;

    auto* provider = m_provider;
    juce::Component::SafePointer<BeatMateLiveView> safe(this);
    const std::string t0 = title.toStdString();
    const std::string a0 = artist.toStdString();

    auto job = [safe, provider, key, t0, a0]()
    {
        auto similars = ChartsService::instance().getSimilarTracks(t0, a0, 40);
        if (similars.empty()) return;

        std::map<std::string, Models::Track> lib;
        if (provider != nullptr) {
            try {
                for (auto& t : provider->getAllTracks())
                    lib.emplace(ChartsService::matchKey(t.title, t.artist), t);
            } catch (...) {}
        }

        auto out = std::make_shared<std::vector<SuggestionEntry>>();
        out->reserve(similars.size());
        for (const auto& s : similars) {
            SuggestionEntry e;
            e.title      = juce::String::fromUTF8(s.title.c_str());
            e.artist     = juce::String::fromUTF8(s.artist.c_str());
            e.reason     = juce::String::fromUTF8(s.reason.c_str());
            e.baseReason = e.reason;
            e.previewUrl = juce::String(s.previewUrl);
            auto it = lib.find(ChartsService::matchKey(s.title, s.artist));
            if (it != lib.end()) {
                const auto& lt = it->second;
                e.inLibrary       = true;
                e.streamingSource = "local";
                e.filePath        = juce::String(lt.filePath);
                e.bpm             = lt.bpm;
                e.energy          = lt.energy;
                e.key = juce::String(lt.camelotKey.empty() ? lt.key : lt.camelotKey);
                e.score = s.sameArtist ? 0.92f : 0.78f;
            } else {
                e.streamingSource = "deezer";
                e.score = s.sameArtist ? 0.62f : 0.5f;
            }
            e.relevance = e.score;
            out->push_back(std::move(e));
        }

        juce::MessageManager::callAsync([safe, out, key]()
        {
            auto* self = safe.getComponent();
            if (!self) return;
            if (self->lastSimilarKey_ != key) return;
            using Services::Streaming::ChartsService;
            std::set<std::string> have;
            for (const auto& s : self->suggestions_)
                have.insert(ChartsService::matchKey(s.title.toStdString(),
                                                    s.artist.toStdString()));
            for (auto& e : *out) {
                const auto k = ChartsService::matchKey(e.title.toStdString(),
                                                       e.artist.toStdString());
                if (!have.insert(k).second) {
                    for (auto& ex : self->suggestions_) {
                        if (ChartsService::matchKey(ex.title.toStdString(),
                                                    ex.artist.toStdString()) != k)
                            continue;
                        ex.score = juce::jmin(1.0f, juce::jmax(ex.score, e.score) + 0.08f);
                        ex.relevance = ex.score;
                        if (e.baseReason.isNotEmpty() && !ex.baseReason.contains(e.baseReason))
                            ex.baseReason = ex.baseReason.isEmpty()
                                ? e.baseReason
                                : ex.baseReason + juce::String::fromUTF8(" \xc2\xb7 ") + e.baseReason;
                        if (e.baseReason.isNotEmpty() && !ex.reason.contains(e.baseReason))
                            ex.reason = ex.reason.isEmpty()
                                ? e.baseReason
                                : ex.reason + juce::String::fromUTF8(" \xc2\xb7 ") + e.baseReason;
                        break;
                    }
                    continue;
                }
                self->suggestions_.push_back(std::move(e));
            }
            std::stable_sort(self->suggestions_.begin(), self->suggestions_.end(),
                [](const SuggestionEntry& a, const SuggestionEntry& b) {
                    const bool la = a.inLibrary || a.filePath.isNotEmpty();
                    const bool lb = b.inLibrary || b.filePath.isNotEmpty();
                    if (la != lb) return la;
                    return a.score > b.score;
                });
            if (self->suggestions_.size() > 80)
                self->suggestions_.resize(80);
            self->updateSuggestions();
        });
    };

    if (auto* pool = ::BeatMate::getBackgroundPool())
        pool->addJob(job);
    else
        std::thread(job).detach();
}

void BeatMateLiveView::loadTrendingFromCollection()
{
    trending_.clear();
    if (m_provider == nullptr) {
        trendingList_->updateContent();
        trendingList_->repaint();
        return;
    }

    const juce::String selectedGenre = genreFilter_->getText();
    const bool isAll = selectedGenre.isEmpty()
                       || selectedGenre.containsIgnoreCase("All Genres")
                       || selectedGenre.containsIgnoreCase("Tous")
                       || genreFilter_->getSelectedId() == 1;

    auto* provider = m_provider;
    juce::WeakReference<juce::Component> weakSelf { this };

    auto job = [weakSelf, provider, selectedGenre, isAll]()
    {
        auto out = std::make_shared<std::vector<TrendingTrack>>();
        try {
            auto allTracks = provider->getAllTracks();

            if (!isAll)
            {
                std::vector<Models::Track> filtered;
                auto genreStr = selectedGenre.toStdString();
                for (const auto& t : allTracks)
                {
                    if (t.genre == genreStr ||
                        juce::String(t.genre).containsIgnoreCase(selectedGenre))
                        filtered.push_back(t);
                }
                allTracks = std::move(filtered);
            }

            struct ScoredTrack {
                Models::Track track;
                float trendScore = 0.0f;
            };
            std::vector<ScoredTrack> scored;
            scored.reserve(allTracks.size());

            auto now = std::chrono::system_clock::now();

            for (const auto& t : allTracks)
            {
                float playScore = std::min(1.0f, static_cast<float>(t.playCount) / 20.0f);

                float recencyScore = 0.5f;
                if (t.lastPlayed > 0) {
                    auto lastPlayed = std::chrono::system_clock::from_time_t(static_cast<time_t>(t.lastPlayed));
                    auto daysSince = std::chrono::duration_cast<std::chrono::hours>(now - lastPlayed).count() / 24;
                    recencyScore = std::max(0.1f, 1.0f - static_cast<float>(daysSince) / 90.0f);
                }

                float energyFactor = 0.5f + t.energy * 0.1f;

                float total = playScore * 0.4f + recencyScore * 0.4f + energyFactor * 0.2f;
                if (total > 0.05f)
                    scored.push_back({ t, total });
            }

            std::sort(scored.begin(), scored.end(),
                [](const auto& a, const auto& b) { return a.trendScore > b.trendScore; });

            int limit = std::min(20, static_cast<int>(scored.size()));
            for (int i = 0; i < limit; ++i)
            {
                const auto& s = scored[static_cast<size_t>(i)];
                TrendingTrack tt;
                tt.position = i + 1;
                tt.title = juce::String(s.track.title);
                tt.artist = juce::String(s.track.artist);
                tt.bpm = s.track.bpm;
                tt.key = juce::String(s.track.camelotKey.empty() ? s.track.key : s.track.camelotKey);
                tt.genre = juce::String(s.track.genre);
                tt.energy = s.track.energy;
                tt.trendScore = s.trendScore;
                tt.playCount = s.track.playCount;
                out->push_back(std::move(tt));
            }
        } catch (...) {}

        juce::MessageManager::callAsync([weakSelf, out]()
        {
            auto* self = dynamic_cast<BeatMateLiveView*>(weakSelf.get());
            if (!self) return;
            self->trending_ = std::move(*out);
            self->trendingLoadedOnce_ = true;
            if (self->trendingDateLbl_)
                self->trendingDateLbl_->setText(
                    juce::String::fromUTF8("Classement de votre collection locale (\xc3\xa9""coutes r\xc3\xa9""centes)"),
                    juce::dontSendNotification);
            if (self->trendingList_) {
                self->trendingList_->updateContent();
                self->trendingList_->repaint();
            }
            if (self->tabContentTrending_) self->tabContentTrending_->repaint();
        });
    };

    if (auto* pool = ::BeatMate::getBackgroundPool())
        pool->addJob(job);
    else
        job();
}

void BeatMateLiveView::showSettingsPanel()
{
    // Owning container for the settings popup — CallOutBox keeps a reference
    class SettingsPanel : public juce::Component
    {
    public:
        SettingsPanel(BeatMateLiveView& owner) : owner_(owner)
        {
            setSize(320, 240);

            srcLabel_.setText(BM_TJ("settings.djSoftware") + ":", juce::dontSendNotification);
            srcLabel_.setBounds(10, 10, 80, 24);
            srcLabel_.setColour(juce::Label::textColourId, Colors::textPrimary());
            addAndMakeVisible(srcLabel_);

            srcCombo_.addItem("VirtualDJ", 1);
            srcCombo_.addItem("Rekordbox", 2);
            srcCombo_.addItem("Serato", 3);
            srcCombo_.addItem("Traktor", 4);
            srcCombo_.addItem("Engine DJ", 5);
            if      (owner_.currentSource_ == "VirtualDJ") srcCombo_.setSelectedId(1);
            else if (owner_.currentSource_ == "Rekordbox") srcCombo_.setSelectedId(2);
            else if (owner_.currentSource_ == "Serato")    srcCombo_.setSelectedId(3);
            else if (owner_.currentSource_ == "Traktor")   srcCombo_.setSelectedId(4);
            else                                            srcCombo_.setSelectedId(5);
            srcCombo_.setBounds(100, 10, 160, 24);
            srcCombo_.onChange = [this]
            {
                if (owner_.sourceSelector_ == nullptr) return;
                int selectorId = 4;
                switch (srcCombo_.getSelectedId())
                {
                    case 1: selectorId = 4; break;
                    case 2: selectorId = 1; break;
                    case 3: selectorId = 2; break;
                    case 4: selectorId = 3; break;
                    case 5: selectorId = 6; break;
                }
                owner_.sourceSelector_->setSelectedId(selectorId, juce::sendNotification);
            };
            addAndMakeVisible(srcCombo_);

            aotToggle_.setButtonText(BM_TJ("live.alwaysOnTop"));
            aotToggle_.setToggleState(owner_.alwaysOnTop_, juce::dontSendNotification);
            aotToggle_.setBounds(10, 44, 250, 24);
            aotToggle_.setColour(juce::ToggleButton::textColourId, Colors::textPrimary());
            aotToggle_.onClick = [this]
            {
                owner_.alwaysOnTop_ = aotToggle_.getToggleState();
                if (auto* w = owner_.findParentComponentOfClass<juce::DocumentWindow>())
                    w->setAlwaysOnTop(owner_.alwaysOnTop_);
                if (owner_.alwaysOnTopToggle_)
                    owner_.alwaysOnTopToggle_->setToggleState(owner_.alwaysOnTop_, juce::dontSendNotification);
                Prefs::setBool("live.alwaysOnTop", owner_.alwaysOnTop_);
            };
            addAndMakeVisible(aotToggle_);

            pollLabel_.setText(BM_TJ("live.settings.pollingInterval"), juce::dontSendNotification);
            pollLabel_.setBounds(10, 78, 150, 24);
            pollLabel_.setColour(juce::Label::textColourId, Colors::textPrimary());
            pollLabel_.setTooltip(BM_TJ("live.settings.pollingIntervalTip"));
            addAndMakeVisible(pollLabel_);

            pollSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
            pollSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
            pollSlider_.setRange(1.0, 10.0, 1.0);
            pollSlider_.setValue(static_cast<double>(owner_.pollingIntervalSec_));
            pollSlider_.setBounds(10, 104, 250, 24);
            pollSlider_.setTooltip(BM_TJ("live.settings.pollingIntervalTip"));
            pollSlider_.onValueChange = [this]
            {
                owner_.pollingIntervalSec_ = static_cast<int>(pollSlider_.getValue());
                Prefs::setInt("live.pollingIntervalSec", owner_.pollingIntervalSec_);
            };
            addAndMakeVisible(pollSlider_);

            autoHideLabel_.setText(BM_TJ("live.autoHide.title"),
                                   juce::dontSendNotification);
            autoHideLabel_.setBounds(10, 138, 300, 20);
            autoHideLabel_.setColour(juce::Label::textColourId, Colors::textPrimary());
            addAndMakeVisible(autoHideLabel_);

            autoHideCombo_.addItem(BM_TJ("live.autoHide.nothing"), 1);
            autoHideCombo_.addItem(BM_TJ("live.autoHide.fade"),    2);
            autoHideCombo_.addItem(BM_TJ("live.autoHide.taskbar"), 3);
            autoHideCombo_.addItem(BM_TJ("live.autoHide.dock"),    4);

            int initial = 2; // Fade default
            if (auto* w = owner_.findParentComponentOfClass<BeatMateLiveWindow>())
            {
                using P = BeatMateLiveWindow::AutoHidePolicy;
                switch (w->getAutoHidePolicy())
                {
                    case P::Disabled:       initial = 1; break;
                    case P::Fade:           initial = 2; break;
                    case P::MinimizeToTray: initial = 3; break;
                    case P::CompactDock:    initial = 4; break;
                }
            }
            else
            {
                extern BeatMate::ServiceLocator* g_serviceLocator;
                if (g_serviceLocator)
                {
                    if (auto* sm = g_serviceLocator
                            ->tryGet<Services::Config::SettingsManager>())
                    {
                        auto s = juce::String(sm->get<std::string>(
                            "beatmate.live.autoHidePolicy", std::string("Fade")));
                        auto p = BeatMateLiveWindow::policyFromString(s);
                        using P = BeatMateLiveWindow::AutoHidePolicy;
                        switch (p)
                        {
                            case P::Disabled:       initial = 1; break;
                            case P::Fade:           initial = 2; break;
                            case P::MinimizeToTray: initial = 3; break;
                            case P::CompactDock:    initial = 4; break;
                        }
                    }
                }
            }
            updateBtn_.setButtonText(juce::String::fromUTF8("Verifier les mises a jour"));
            updateBtn_.setBounds(10, 198, 300, 30);
            updateBtn_.setColour(juce::TextButton::buttonColourId, Colors::primary());
            updateBtn_.onClick = [this] { owner_.checkForUpdatesFlow(); };
            addAndMakeVisible(updateBtn_);

            autoHideCombo_.setSelectedId(initial, juce::dontSendNotification);
            autoHideCombo_.setBounds(10, 162, 300, 24);
            autoHideCombo_.onChange = [this]
            {
                using P = BeatMateLiveWindow::AutoHidePolicy;
                P p = P::Fade;
                switch (autoHideCombo_.getSelectedId())
                {
                    case 1: p = P::Disabled;       break;
                    case 2: p = P::Fade;           break;
                    case 3: p = P::MinimizeToTray; break;
                    case 4: p = P::CompactDock;    break;
                }
                if (auto* w = owner_.findParentComponentOfClass<BeatMateLiveWindow>())
                {
                    w->setAutoHidePolicy(p);
                }
                else
                {
                    extern BeatMate::ServiceLocator* g_serviceLocator;
                    if (g_serviceLocator)
                        if (auto* sm = g_serviceLocator
                                ->tryGet<Services::Config::SettingsManager>())
                            sm->set<std::string>(
                                "beatmate.live.autoHidePolicy",
                                BeatMateLiveWindow::policyToString(p).toStdString());
                }
            };
            addAndMakeVisible(autoHideCombo_);
        }

    private:
        BeatMateLiveView& owner_;
        juce::Label        srcLabel_;
        juce::ComboBox     srcCombo_;
        juce::ToggleButton aotToggle_;
        juce::Label        pollLabel_;
        juce::Slider       pollSlider_;
        juce::Label        autoHideLabel_;
        juce::ComboBox     autoHideCombo_;
        juce::TextButton   updateBtn_;
    };

    auto content = std::make_unique<SettingsPanel>(*this);
    // Anchor to the view so the CallOutBox shares the Live window's Z-order
    auto area = getLocalArea(settingsButton_.get(), settingsButton_->getLocalBounds());
    auto& cb = juce::CallOutBox::launchAsynchronously(
        std::move(content),
        area,
        this);
    cb.setDismissalMouseClicksAreAlwaysConsumed(false);
}

void BeatMateLiveView::checkForUpdatesFlow()
{
    std::string base = "https://beatmate.fr/beatmate-mise-a-jour";
    extern BeatMate::ServiceLocator* g_serviceLocator;
    if (g_serviceLocator)
        if (auto* sm = g_serviceLocator->tryGet<Services::Config::SettingsManager>())
        {
            const auto v = sm->get<std::string>("general.updateBaseUrl", std::string());
            if (! v.empty()) base = v;
        }

    auto svc = std::make_shared<Services::Update::UpdateService>(std::string(BEATMATE_VERSION));
    svc->setBaseUrl(base);

    juce::Component::SafePointer<BeatMateLiveView> self(this);
    svc->checkRemoteAsync([self, svc](Services::Update::UpdateInfo info)
    {
        if (self == nullptr) return;
        if (! info.error.empty())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                juce::String::fromUTF8("Mise a jour"),
                juce::String::fromUTF8(("Erreur : " + info.error).c_str()));
            return;
        }
        if (! info.available)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                juce::String::fromUTF8("Mise a jour"),
                juce::String::fromUTF8("BeatMate est a jour."));
            return;
        }
        juce::String msg;
        msg << juce::String::fromUTF8("Nouvelle version disponible : ") << juce::String(info.latestVersion)
            << juce::String::fromUTF8(".\n\nTelecharger et installer maintenant ? L'application se fermera pour terminer.");
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,
            juce::String::fromUTF8("Mise a jour BeatMate"), msg,
            juce::String::fromUTF8("Mettre a jour"), juce::String::fromUTF8("Annuler"), nullptr,
            juce::ModalCallbackFunction::create([svc, info](int result)
            {
                if (result != 1) return;
                svc->downloadAndInstallAsync(info.downloadUrl,
                    [](bool ok, std::string)
                    {
                        if (ok)
                            if (auto* app = juce::JUCEApplicationBase::getInstance())
                                app->systemRequestedQuit();
                    });
            }));
    });
}

void BeatMateLiveView::showHelpPopup()
{
    juce::String helpText = BM_TJ("live.help.body");
    helpText << "\n\nBeatMate V" << BEATMATE_VERSION;

    auto aw = std::make_shared<juce::AlertWindow>(BM_TJ("live.help.title"),
                                                  helpText,
                                                  juce::MessageBoxIconType::InfoIcon);
    aw->addButton("OK", 0, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton(juce::String::fromUTF8("Verifier les mises a jour"), 1);

    juce::Component::SafePointer<BeatMateLiveView> self(this);
    aw->enterModalState(true,
        juce::ModalCallbackFunction::create([self, aw](int result)
        {
            aw->setVisible(false);
            if (result == 1 && self != nullptr)
                self->checkForUpdatesFlow();
        }),
        false);
}

void BeatMateLiveView::showSessionExportMenu()
{
    juce::PopupMenu m;
    m.addItem(1, "Exporter en M3U8");
    m.addItem(2, "Exporter pour Rekordbox (XML)");
    m.addItem(3, "Exporter pour Serato (.crate)");
    m.addItem(4, "Exporter pour Traktor (NML)");
    m.addItem(5, "Exporter en CSV");

    juce::Component::SafePointer<BeatMateLiveView> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options()
                        .withTargetComponent(exportSessionBtn_.get()),
        [safe](int result)
        {
            auto* self = safe.getComponent();
            if (!self || result <= 0) return;

            extern BeatMate::ServiceLocator* g_serviceLocator;
            Services::History::SessionHistoryRecorder* recorder = nullptr;
            Services::History::SessionExporter*        exporter = nullptr;
            if (g_serviceLocator)
            {
                recorder = g_serviceLocator->tryGet<Services::History::SessionHistoryRecorder>();
                exporter = g_serviceLocator->tryGet<Services::History::SessionExporter>();
            }
            if (recorder == nullptr || exporter == nullptr)
            {
                Utils::showAutoCloseAlert(juce::MessageBoxIconType::WarningIcon,
                    juce::String::fromUTF8("Export impossible"),
                    juce::String::fromUTF8("Service d'export indisponible."), 5000);
                return;
            }

            auto cur = recorder->currentSession();
            if (!cur.has_value())
            {
                Utils::showAutoCloseAlert(juce::MessageBoxIconType::WarningIcon,
                    juce::String::fromUTF8("Export impossible"),
                    juce::String::fromUTF8("Aucune session active : lancez d'abord un set dans la vue Live."),
                    6000);
                return;
            }

            juce::String defaultName, ext, title;
            switch (result)
            {
                case 2:  defaultName = "session_rekordbox.xml";  ext = "*.xml";
                         title = "Exporter pour Rekordbox";      break;
                case 3:  defaultName = "BeatMateSession.crate";  ext = "*.crate";
                         title = "Exporter pour Serato";         break;
                case 4:  defaultName = "session_traktor.nml";    ext = "*.nml";
                         title = "Exporter pour Traktor";        break;
                case 5:  defaultName = "session.csv";            ext = "*.csv";
                         title = "Exporter en CSV";              break;
                case 1:
                default: defaultName = "session.m3u8";           ext = "*.m3u8";
                         title = "Exporter en M3U8";             break;
            }

            auto docs = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                            .getChildFile("BeatMate").getChildFile("Exports");
            docs.createDirectory();

            self->sessionExportChooser_ = std::make_unique<juce::FileChooser>(
                title, docs.getChildFile(defaultName), ext);

            const std::string sid = cur->sessionId;
            const int fmt = result;
            self->sessionExportChooser_->launchAsync(
                juce::FileBrowserComponent::saveMode
                    | juce::FileBrowserComponent::canSelectFiles
                    | juce::FileBrowserComponent::warnAboutOverwriting,
                [exporter, sid, fmt](const juce::FileChooser& fc)
                {
                    auto file = fc.getResult();
                    if (file == juce::File{}) return;

                    bool ok = false;
                    switch (fmt)
                    {
                        case 2:  ok = exporter->exportAsRekordboxXML(sid, file); break;
                        case 3:  ok = exporter->exportAsSeratoCrate(sid, file);  break;
                        case 4:  ok = exporter->exportAsTraktorNML(sid, file);   break;
                        case 5:  ok = exporter->exportAsCSV(sid, file);          break;
                        case 1:
                        default: ok = exporter->exportAsM3U8(sid, file);         break;
                    }

                    if (ok)
                    {
                        file.getParentDirectory().revealToUser();
                        Utils::showAutoCloseAlert(juce::MessageBoxIconType::InfoIcon,
                            juce::String::fromUTF8("Session export\xc3\xa9""e"),
                            juce::String::fromUTF8("Fichier \xc3\xa9""crit :\n")
                                + file.getFullPathName(), 6000);
                    }
                    else
                    {
                        Utils::showAutoCloseAlert(juce::MessageBoxIconType::WarningIcon,
                            juce::String::fromUTF8("\xc3\x89""chec de l'export"),
                            juce::String::fromUTF8("Aucune piste \xc3\xa0 exporter ou \xc3\xa9""criture impossible :\n")
                                + file.getFullPathName(), 7000);
                    }
                });
        });
}

void BeatMateLiveView::mouseDown(const juce::MouseEvent& event)
{
    if (!compactMode_)
    {
        if (!statusChipBounds_.isEmpty()
            && statusChipBounds_.expanded(4).contains(event.getPosition()))
        {
            juce::String statusMsg;
            statusMsg << BM_TJ("live.status.label") << " " << (nowPlaying_.connected ? BM_TJ("live.status.connected") : BM_TJ("live.status.disconnected")) << "\n"
                      << BM_TJ("live.status.source") << " " << currentSource_ << "\n"
                      << BM_TJ("live.status.monitoring") << " " << (monitoring_ ? BM_TJ("live.status.active") : BM_TJ("live.status.inactive")) << "\n"
                      << BM_TJ("live.status.tracksPlayed") << " " << juce::String(totalTracksPlayed_) << "\n"
                      << BM_TJ("live.status.avgBpm") << " " << juce::String(averageBPM_, 1);

            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                                                    BM_TJ("live.status.title"),
                                                    statusMsg, "OK");
            return;
        }
    }

    juce::Component::mouseDown(event);
}

void BeatMateLiveView::switchBottomTab(BottomTab tab)
{
    activeBottomTab_ = tab;

    tabSuggestions_->setColour(juce::TextButton::buttonColourId,
                                tab == BottomTab::Suggestions ? Colors::primary() : Colors::bgLighter());
    tabTrending_->setColour(juce::TextButton::buttonColourId,
                             tab == BottomTab::Trending ? Colors::primary() : Colors::bgLighter());
    tabHistory_->setColour(juce::TextButton::buttonColourId,
                            tab == BottomTab::History ? Colors::primary() : Colors::bgLighter());

    suggestionList_->setVisible(tab == BottomTab::Suggestions);

    trendingList_->setVisible(tab == BottomTab::Trending);
    genreFilter_->setVisible(tab == BottomTab::Trending);
    countryFilter_->setVisible(tab == BottomTab::Trending);

    if (tab == BottomTab::Trending) {
        if (trending_.empty()) loadTrendingFromDiskCache();
        reloadTrendingSelection(false);
    }

    historyList_->setVisible(tab == BottomTab::History);

    resized();
    repaint();
}

void BeatMateLiveView::toggleCompactMode()
{
    compactMode_ = !compactMode_;

    compactToggle_->setButtonText(compactMode_ ? BM_TJ("live.modeFull") : BM_TJ("live.modeCompact"));

    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
    {
        if (compactMode_)
        {
            window->setResizeLimits(180, 180, 1200, 1400);
            window->centreWithSize(200, 200);
        }
        else
        {
            window->setResizeLimits(720, 500, 2400, 2000);
            window->centreWithSize(900, 720);
        }
    }
    resized();
    repaint();
}


void BeatMateLiveView::paintGlassPanel(juce::Graphics& g, juce::Rectangle<int> area)
{
    auto bounds = area.toFloat();

    g.setColour(juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.03f));
    g.fillRoundedRectangle(bounds, 12.0f);

    g.setColour(juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.06f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 12.0f, 1.0f);

    g.setColour(juce::Colour(0xFF0A0A12).withAlpha(0.4f));
    g.drawLine(bounds.getX() + 12, bounds.getBottom() - 0.5f,
               bounds.getRight() - 12, bounds.getBottom() - 0.5f, 1.0f);
}


void BeatMateLiveView::paint(juce::Graphics& g)
{
    ProDraw::viewBackground(g, getWidth(), getHeight());

    const bool forcedCompact = (getWidth() < 320 || getHeight() < 180);
    if (compactMode_ || forcedCompact)
    {
        paintCompactView(g);
        return;
    }

    auto area = getLocalBounds().reduced(12);

    auto headerArea = area.removeFromTop(48);
    paintHeader(g, headerArea);

    area.removeFromTop(8);


    auto body = area;
    const bool narrow = body.getWidth() < 780;

    if (!narrow)
    {
        const int leftW = juce::jmax(280, static_cast<int>(body.getWidth() * 0.35f));
        auto leftCol = body.removeFromLeft(leftW);
        paintGlassPanel(g, leftCol);
        paintNowPlaying(g, leftCol);
    }
    else
    {
        auto npArea = body.removeFromTop(200);
        paintGlassPanel(g, npArea);
        paintNowPlaying(g, npArea);
    }
}


void BeatMateLiveView::paintHeader(juce::Graphics& g, juce::Rectangle<int> area)
{
    auto titleArea = area.removeFromLeft(200);
    g.setColour(Colors::textPrimary());
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(20.0f).withStyle("Bold")));
    g.drawText(BM_TJ("live.suggest.title"), titleArea.removeFromTop(28), juce::Justification::centredLeft);

    g.setColour(Colors::textDim());
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText(juce::String::fromUTF8("par S\xc3\xa9""bastien Sainte-Foi"), titleArea, juce::Justification::centredLeft);

    const bool connected = nowPlaying_.connected;
    juce::String stText = connected ? BM_TJ("live.status.connected")
                                    : BM_TJ("live.status.disconnected");
    const juce::String shownSource = activeDetectedSource_.isNotEmpty()
        ? activeDetectedSource_ : currentSource_;
    if (connected && shownSource.isNotEmpty())
        stText << juce::String::fromUTF8(" \xc2\xb7 ") << shownSource;

    juce::Font stFont(juce::FontOptions{}.withHeight(10.0f).withStyle("Bold"));
    const int chipW = stFont.getStringWidth(stText) + 36;
    auto chip = area.removeFromLeft(chipW).withSizeKeepingCentre(chipW, 22);
    statusChipBounds_ = chip;

    const juce::Colour stCol = connected ? Colors::success() : Colors::textMuted();
    g.setColour(stCol.withAlpha(0.10f));
    g.fillRoundedRectangle(chip.toFloat(), 11.0f);
    g.setColour(stCol.withAlpha(0.35f));
    g.drawRoundedRectangle(chip.toFloat().reduced(0.5f), 11.0f, 1.0f);

    auto chipInner = chip.reduced(8, 0);
    auto ledArea = chipInner.removeFromLeft(10).withSizeKeepingCentre(8, 8);
    paintConnectionLED(g, ledArea, connected);

    chipInner.removeFromLeft(6);
    g.setColour(stCol);
    g.setFont(stFont);
    g.drawText(stText, chipInner, juce::Justification::centredLeft);

    area.removeFromLeft(10);
    int sparkRight = area.getRight();
    if (sourceSelector_ && sourceSelector_->isVisible())
        sparkRight = juce::jmin(sparkRight, sourceSelector_->getX() - 12);
    const int avail = sparkRight - area.getX();
    if (avail >= 70)
        paintSessionSparkline(g, area.withWidth(juce::jmin(avail, 220)));
}

void BeatMateLiveView::paintSessionSparkline(juce::Graphics& g, juce::Rectangle<int> area)
{
    std::vector<float> series;
    series.reserve(history_.size());
    for (auto it = history_.rbegin(); it != history_.rend(); ++it)
        if (it->energy > 0.0f) series.push_back(it->energy);
    bool isEnergy = series.size() >= 3;
    if (!isEnergy)
    {
        series.clear();
        for (auto it = history_.rbegin(); it != history_.rend(); ++it)
            if (it->bpm > 0.0) series.push_back(static_cast<float>(it->bpm));
    }
    if (series.size() < 3) return;
    if (series.size() > 24) series.erase(series.begin(), series.end() - 24);

    float mn = series[0], mx = series[0];
    for (float v : series) { mn = juce::jmin(mn, v); mx = juce::jmax(mx, v); }
    const float span = juce::jmax(0.001f, mx - mn);

    float slope = 0.0f;
    const int nDeltas = juce::jmin(3, static_cast<int>(series.size()) - 1);
    for (int i = 0; i < nDeltas; ++i)
        slope += series[series.size() - 1 - static_cast<size_t>(i)]
               - series[series.size() - 2 - static_cast<size_t>(i)];
    slope /= static_cast<float>(nDeltas);
    const float thresh = isEnergy ? 0.3f : 1.5f;

    juce::String trendTxt;
    juce::Colour trendCol;
    if (slope > thresh)       { trendTxt = juce::String::fromUTF8("\xe2\x86\x97 Monter");      trendCol = Colors::success(); }
    else if (slope < -thresh) { trendTxt = juce::String::fromUTF8("\xe2\x86\x98 Redescendre"); trendCol = Colors::warning(); }
    else                      { trendTxt = juce::String::fromUTF8("\xe2\x86\x92 Maintenir");   trendCol = Colors::textSecondary(); }

    auto inner = area.reduced(0, 8);
    auto labelArea = inner.removeFromBottom(11);
    auto plot = inner.toFloat();

    juce::Path p;
    const float stepX = plot.getWidth() / static_cast<float>(series.size() - 1);
    for (size_t i = 0; i < series.size(); ++i)
    {
        const float x = plot.getX() + stepX * static_cast<float>(i);
        const float y = plot.getBottom() - ((series[i] - mn) / span) * plot.getHeight();
        if (i == 0) p.startNewSubPath(x, y);
        else        p.lineTo(x, y);
    }
    g.setColour(Colors::accent().withAlpha(0.75f));
    g.strokePath(p, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved));

    g.setColour(trendCol);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f).withStyle("Bold")));
    g.drawText((isEnergy ? juce::String::fromUTF8("\xc3\x89nergie ") : juce::String("BPM ")) + trendTxt,
               labelArea, juce::Justification::centredLeft);
}

void BeatMateLiveView::paintSetEnergyCurve(juce::Graphics& g, juce::Rectangle<int> area)
{
    std::vector<float> series;
    series.reserve(history_.size());
    for (auto it = history_.rbegin(); it != history_.rend(); ++it)
        if (it->energy > 0.0f) series.push_back(juce::jlimit(0.0f, 10.0f, it->energy));
    if (nowPlaying_.energy > 0.0f)
        series.push_back(juce::jlimit(0.0f, 10.0f, nowPlaying_.energy));

    auto title = area.removeFromTop(12);
    g.setColour(Colors::textSecondary());
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.5f).withStyle("Bold")));
    g.drawText(juce::String::fromUTF8("Courbe d'\xc3\xa9nergie du set (1-10)"),
               title, juce::Justification::centredLeft);

    auto plot = area.reduced(2, 2);
    if (plot.getHeight() < 14) return;

    auto plotF = plot.toFloat();
    auto axis = plotF.removeFromLeft(16.0f);
    g.setColour(Colors::bgLighter().withAlpha(0.35f));
    g.fillRoundedRectangle(plotF, 4.0f);

    for (int lvl = 0; lvl <= 10; lvl += 5)
    {
        const float y = plotF.getBottom() - (lvl / 10.0f) * plotF.getHeight();
        g.setColour(Colors::borderLight().withAlpha(0.25f));
        g.drawLine(plotF.getX(), y, plotF.getRight(), y, 0.6f);
        g.setColour(Colors::textMuted());
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(8.0f)));
        g.drawText(juce::String(lvl), axis.withY(y - 6.0f).withHeight(12.0f),
                   juce::Justification::centredRight);
    }

    if (series.size() < 2)
    {
        g.setColour(Colors::textMuted());
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f).withStyle("italic")));
        g.drawText(juce::String::fromUTF8("Jouez quelques morceaux pour tracer la courbe\xe2\x80\xa6"),
                   plotF.toNearestInt(), juce::Justification::centred);
        return;
    }

    const size_t maxPts = 32;
    if (series.size() > maxPts) series.erase(series.begin(), series.end() - maxPts);

    juce::Path line, fill;
    const float stepX = plotF.getWidth() / static_cast<float>(series.size() - 1);
    for (size_t i = 0; i < series.size(); ++i)
    {
        const float x = plotF.getX() + stepX * static_cast<float>(i);
        const float y = plotF.getBottom() - (series[i] / 10.0f) * plotF.getHeight();
        if (i == 0) { line.startNewSubPath(x, y); fill.startNewSubPath(x, plotF.getBottom()); fill.lineTo(x, y); }
        else        { line.lineTo(x, y); fill.lineTo(x, y); }
    }
    fill.lineTo(plotF.getRight(), plotF.getBottom());
    fill.closeSubPath();

    juce::ColourGradient fillGrad(Colors::accent().withAlpha(0.30f), plotF.getX(), plotF.getY(),
                                  Colors::accent().withAlpha(0.02f), plotF.getX(), plotF.getBottom(), false);
    g.setGradientFill(fillGrad);
    g.fillPath(fill);
    g.setColour(Colors::accent());
    g.strokePath(line, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float lastX = plotF.getX() + stepX * static_cast<float>(series.size() - 1);
    const float lastY = plotF.getBottom() - (series.back() / 10.0f) * plotF.getHeight();
    g.setColour(Colors::success().withAlpha(0.30f));
    g.fillEllipse(lastX - 5.0f, lastY - 5.0f, 10.0f, 10.0f);
    g.setColour(Colors::success());
    g.fillEllipse(lastX - 3.0f, lastY - 3.0f, 6.0f, 6.0f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(8.5f).withStyle("Bold")));
    g.drawText(juce::String(series.back(), 1),
               juce::Rectangle<float>(lastX - 18.0f, lastY - 16.0f, 16.0f, 11.0f).toNearestInt(),
               juce::Justification::centredRight);
}

void BeatMateLiveView::updatePhraseHintCache()
{
    juce::String txt;
    if (smartEngine_ && smartEngine_->getCurrentTrack() > 0)
    {
        auto ph = smartEngine_->detectPhrasesForCurrent();
        if (ph.valid)
        {
            txt = juce::String::fromUTF8("\xe2\x96\xb6 Phrases : ")
                + juce::String(ph.phraseLengthBars)
                + juce::String::fromUTF8(" mesures");
            if (ph.mixOutPointSec >= 0.0)
            {
                const int m = (int) (ph.mixOutPointSec / 60.0);
                const int s = (int) ph.mixOutPointSec % 60;
                txt += juce::String::fromUTF8("  \xc2\xb7  Point de mix \xe2\x86\x92 ")
                     + juce::String(m) + ":" + juce::String(s).paddedLeft('0', 2);
            }
        }
    }
    if (txt.isEmpty())
        txt = juce::String::fromUTF8("\xe2\x96\xb6 Phrases : analyse indisponible (BPM et dur\xc3\xa9""e requis)");
    phraseHintCache_ = txt;
}

void BeatMateLiveView::paintPhraseHint(juce::Graphics& g, juce::Rectangle<int> area)
{
    if (phraseHintCache_.isEmpty())
        updatePhraseHintCache();

    g.setColour(Colors::textSecondary());
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.5f).withStyle("Bold")));
    g.drawText(phraseHintCache_, area, juce::Justification::centredLeft);
}


void BeatMateLiveView::paintConnectionLED(juce::Graphics& g, juce::Rectangle<int> area, bool connected)
{
    auto bounds = area.toFloat();
    juce::Colour ledCol = connected ? Colors::success() : Colors::error();

    float alpha = 1.0f;
    if (connected && monitoring_)
        alpha = ledBlinkState_ ? 1.0f : 0.4f;

    g.setColour(ledCol.withAlpha(0.2f * alpha));
    g.fillEllipse(bounds.expanded(3.0f));

    g.setColour(ledCol.withAlpha(alpha));
    g.fillEllipse(bounds);

    g.setColour(juce::Colours::white.withAlpha(0.3f * alpha));
    g.fillEllipse(bounds.reduced(2.0f).withTrimmedBottom(bounds.getHeight() * 0.5f));
}


void BeatMateLiveView::paintNowPlaying(juce::Graphics& g, juce::Rectangle<int> area)
{
    auto inner = area.reduced(16);

    if (!nowPlaying_.connected)
    {
        auto centre = inner.getCentre();
        auto badgeBounds = juce::Rectangle<float>(
            static_cast<float>(centre.x) - 32.0f,
            static_cast<float>(centre.y) - 70.0f, 64.0f, 64.0f);

        g.setColour(Colors::primary().withAlpha(0.12f));
        g.fillEllipse(badgeBounds.expanded(6.0f));
        g.setColour(Colors::primary().withAlpha(0.25f));
        g.fillEllipse(badgeBounds);
        g.setColour(Colors::primary());
        g.drawEllipse(badgeBounds, 1.5f);
        g.setFont(juce::Font(28.0f));
        g.drawText(juce::CharPointer_UTF8("\xe2\x99\xab"),
                   badgeBounds.toNearestInt(), juce::Justification::centred);

        auto textArea = juce::Rectangle<int>(inner.getX(), centre.y,
                                             inner.getWidth(), 80);
        g.setFont(juce::Font(16.0f, juce::Font::bold));
        g.setColour(Colors::textPrimary());
        g.drawText(BM_TJ("live.noDjSource"),
                   textArea.removeFromTop(24), juce::Justification::centred);
        g.setFont(juce::Font(12.0f));
        g.setColour(Colors::textDim());
        g.drawText(BM_TJ("live.clickConnectPrefix") + " \"" + BM_TJ("live.connectPrefix") + " " + currentSource_ + "\" " + BM_TJ("live.clickConnectSuffix"),
                   textArea.removeFromTop(20), juce::Justification::centred);
        g.setFont(juce::Font(10.5f));
        g.setColour(Colors::textMuted());
        g.drawText(BM_TJ("live.orSelectSource"),
                   textArea.removeFromTop(16), juce::Justification::centred);
        return;
    }

    {
        auto bannerArea = inner.removeFromTop(20);
        const bool hasTitle = nowPlaying_.title.isNotEmpty() && nowPlaying_.title != "---";
        if (hasTitle)
        {
            juce::String banner = juce::String::fromUTF8("EN COURS  \xc2\xb7  ");
            if (nowPlaying_.artist.isNotEmpty() && nowPlaying_.artist != "---")
                banner << nowPlaying_.artist << juce::String::fromUTF8(" \xe2\x80\x94 ");
            banner << nowPlaying_.title;
            const juce::String shownSrc = activeDetectedSource_.isNotEmpty()
                ? activeDetectedSource_ : currentSource_;
            if (shownSrc.isNotEmpty())
                banner << "  (" << shownSrc << ")";
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f).withStyle("Bold")));
            const float pillW = juce::jmin(static_cast<float>(bannerArea.getWidth()),
                juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), banner) + 40.0f);
            ProDraw::statusPill(g, banner,
                juce::Rectangle<float>(static_cast<float>(bannerArea.getX()),
                                       static_cast<float>(bannerArea.getY()), pillW, 18.0f),
                Colors::success(), monitoring_ && ledBlinkState_);
        }
        else
        {
            g.setColour(Colors::textMuted());
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
            g.drawText(juce::String::fromUTF8("Aucun titre d\xc3\xa9tect\xc3\xa9"),
                       bannerArea, juce::Justification::centredLeft);
        }
        inner.removeFromTop(4);
    }

    auto topRow = inner.removeFromTop(110);

    auto artArea = topRow.removeFromLeft(100);
    {
        auto artBounds = artArea.toFloat().withSizeKeepingCentre(100.0f, 100.0f);
        g.setColour(Colors::bgLightest());
        g.fillEllipse(artBounds);
        g.setColour(Colors::borderLight());
        g.drawEllipse(artBounds, 1.0f);

        g.setColour(Colors::textDim());
        g.setFont(juce::Font(32.0f));
        g.drawText(juce::CharPointer_UTF8("\xe2\x99\xab"), artBounds, juce::Justification::centred);
    }

    topRow.removeFromLeft(16);

    auto infoArea = topRow.removeFromLeft(topRow.getWidth() - 140);

    {
        auto deckBadge = juce::Rectangle<float>(static_cast<float>(infoArea.getX()),
                                                  static_cast<float>(infoArea.getY()), 60.0f, 18.0f);
        g.setColour(Colors::primary().withAlpha(0.2f));
        g.fillRoundedRectangle(deckBadge, 9.0f);
        g.setColour(Colors::primary());
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText(nowPlaying_.deck, deckBadge, juce::Justification::centred);
    }

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(22.0f, juce::Font::bold));
    g.drawText(nowPlaying_.title,
               infoArea.getX(), infoArea.getY() + 22, infoArea.getWidth(), 28,
               juce::Justification::centredLeft);

    g.setColour(Colors::textSecondary());
    g.setFont(juce::Font(14.0f));
    g.drawText(nowPlaying_.artist,
               infoArea.getX(), infoArea.getY() + 52, infoArea.getWidth(), 20,
               juce::Justification::centredLeft);

    int badgeY = infoArea.getY() + 76;

    {
        const juce::String bpmText = nowPlaying_.bpm > 0.0
            ? juce::String(nowPlaying_.bpm, 1) + " BPM"
            : juce::String::fromUTF8("\xe2\x80\x94 BPM");
        ProDraw::badge(g, bpmText, static_cast<float>(infoArea.getX()),
                       static_cast<float>(badgeY), 72.0f, 22.0f, Colors::bpmBadge());
    }

    if (nowPlaying_.key.isNotEmpty())
        ProDraw::badge(g, nowPlaying_.key, static_cast<float>(infoArea.getX()) + 80.0f,
                       static_cast<float>(badgeY), 48.0f, 22.0f, Colors::keyBadge());

    auto energyArea = topRow.removeFromRight(120);
    paintEnergyArc(g, energyArea, nowPlaying_.energy);

    inner.removeFromTop(8);

    const bool showSetExtras = cabinMode_ && inner.getHeight() >= 150;
    int waveCap = showSetExtras ? 90 : 110;
    auto waveformArea = inner.removeFromTop(juce::jmin(waveCap, juce::jmax(60, inner.getHeight() - (showSetExtras ? 76 : 20))));
    paintWaveformRGB(g, waveformArea);

    if (showSetExtras)
    {
        inner.removeFromTop(6);
        auto phraseArea = inner.removeFromTop(juce::jmin(20, inner.getHeight()));
        paintPhraseHint(g, phraseArea);
        if (inner.getHeight() >= 40)
        {
            inner.removeFromTop(4);
            paintSetEnergyCurve(g, inner.removeFromTop(juce::jmin(64, inner.getHeight())));
        }
    }
}


void BeatMateLiveView::paintEnergyArc(juce::Graphics& g, juce::Rectangle<int> area, float energy)
{
    auto bounds = area.toFloat().reduced(4.0f);
    float size = juce::jmin(bounds.getWidth(), bounds.getHeight());
    auto centre = bounds.getCentre();
    float radius = size * 0.4f;
    float arcThickness = 5.0f;

    float startAngle = juce::MathConstants<float>::pi * 0.75f;
    float endAngle = juce::MathConstants<float>::pi * 2.25f;
    float fullSweep = endAngle - startAngle;

    juce::Path bgArc;
    bgArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour(Colors::bgLightest().withAlpha(0.3f));
    g.strokePath(bgArc, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    float normalised = energy / 10.0f;
    if (normalised > 0.0f)
    {
        float valueAngle = startAngle + fullSweep * normalised;
        int segments = juce::jmax(1, static_cast<int>(normalised * 20.0f));
        float segAngle = (valueAngle - startAngle) / static_cast<float>(segments);

        for (int i = 0; i < segments; ++i)
        {
            float t = static_cast<float>(i) / static_cast<float>(juce::jmax(1, segments - 1));
            float a1 = startAngle + segAngle * static_cast<float>(i);
            float a2 = a1 + segAngle + 0.02f;

            juce::Path seg;
            seg.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, a1, a2, true);

            juce::Colour col;
            float ev = t * normalised;
            if (ev < 0.5f)
                col = Colors::success().interpolatedWith(Colors::warning(), ev * 2.0f);
            else
                col = Colors::warning().interpolatedWith(Colors::error(), (ev - 0.5f) * 2.0f);

            g.setColour(col);
            g.strokePath(seg, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
        }
    }

    g.setColour(Colors::warning());
    g.setFont(juce::Font(22.0f, juce::Font::bold));
    g.drawText(juce::String(energy, 1), bounds.reduced(radius * 0.2f), juce::Justification::centred);

    g.setColour(Colors::textDim());
    g.setFont(juce::Font(8.0f, juce::Font::bold));
    g.drawText("ENERGY", juce::Rectangle<float>(centre.x - 25.0f, centre.y + 10.0f, 50.0f, 12.0f),
               juce::Justification::centred);
}


void BeatMateLiveView::paintWaveformRGB(juce::Graphics& g, juce::Rectangle<int> area)
{
    auto bounds = area.toFloat();

    g.setColour(Colors::bgDark());
    g.fillRoundedRectangle(bounds, 8.0f);

    float w = bounds.getWidth();
    float h = bounds.getHeight();
    float centreY = bounds.getY() + h * 0.5f;

    size_t numSamples = waveformLow_.size();
    if (numSamples == 0) return;

    float xScale = w / static_cast<float>(numSamples);

    auto drawBand = [&](const std::vector<float>& data, juce::Colour colour, float alpha)
    {
        g.setColour(colour.withAlpha(alpha));
        for (size_t i = 0; i < data.size(); ++i)
        {
            size_t idx = (i + static_cast<size_t>(waveformAnimOffset_)) % data.size();
            float val = data[idx] * (h * 0.45f);
            float x = bounds.getX() + static_cast<float>(i) * xScale;
            float barW = juce::jmax(1.0f, xScale - 0.3f);
            g.fillRect(x, centreY - val, barW, val * 2.0f);
        }
    };

    drawBand(waveformLow_,  Colors::waveformBass(),   0.55f);
    drawBand(waveformMid_,  Colors::waveformMid(),    0.4f);
    drawBand(waveformHigh_, Colors::waveformTreble(), 0.35f);

    float playX = bounds.getX() + playheadPosition_ * w;
    g.setColour(juce::Colours::white.withAlpha(0.8f));
    g.drawVerticalLine(static_cast<int>(playX), bounds.getY(), bounds.getBottom());

    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.fillRect(playX - 3.0f, bounds.getY(), 6.0f, h);

    g.setColour(juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.04f));
    g.drawRoundedRectangle(bounds, 8.0f, 1.0f);
}


void BeatMateLiveView::paintHistoryStats(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.06f));
    g.drawHorizontalLine(area.getY(), static_cast<float>(area.getX() + 16),
                         static_cast<float>(area.getRight() - 16));

    auto inner = area.reduced(16, 8);
    int colW = inner.getWidth() / 3;

    auto col1 = inner.removeFromLeft(colW);
    g.setColour(Colors::textDim());
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawText(BM_TJ("live.totalTracks"), col1.removeFromTop(14), juce::Justification::centred);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(16.0f, juce::Font::bold));
    g.drawText(juce::String(totalTracksPlayed_), col1, juce::Justification::centred);

    auto col2 = inner.removeFromLeft(colW);
    g.setColour(Colors::textDim());
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawText(BM_TJ("live.setDuration"), col2.removeFromTop(14), juce::Justification::centred);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(16.0f, juce::Font::bold));
    g.drawText(totalSetDuration_, col2, juce::Justification::centred);

    auto col3 = inner;
    g.setColour(Colors::textDim());
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawText(BM_TJ("live.bpmAvg"), col3.removeFromTop(14), juce::Justification::centred);
    g.setColour(juce::Colour(0xFF00D9FF));
    g.setFont(juce::Font(16.0f, juce::Font::bold));
    g.drawText(juce::String(averageBPM_, 1), col3, juce::Justification::centred);
}


void BeatMateLiveView::paintCompactView(juce::Graphics& g)
{
    const int h = getHeight();

    if (h < 140)
    {
        auto area = getLocalBounds().reduced(28, 6);

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(14.0f).withStyle("Bold")));
        auto titleArea = area.removeFromTop(20);
        g.drawText(nowPlaying_.title.isNotEmpty() ? nowPlaying_.title : juce::String("---"),
                   titleArea, juce::Justification::centredLeft, true);

        g.setColour(juce::Colour(0xFFB0B0C8));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
        auto artistArea = area.removeFromTop(14);
        g.drawText(nowPlaying_.artist, artistArea, juce::Justification::centredLeft, true);

        area.removeFromTop(4);

        auto badgeRow = area.removeFromTop(22);
        auto drawPill = [&](juce::Rectangle<int> r, juce::Colour tint, const juce::String& text) {
            juce::Rectangle<float> rf = r.toFloat();
            g.setColour(tint.withAlpha(0.2f));
            g.fillRoundedRectangle(rf, 11.0f);
            g.setColour(tint);
            g.drawRoundedRectangle(rf, 11.0f, 1.2f);
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f).withStyle("Bold")));
            g.drawText(text, rf, juce::Justification::centred, false);
        };
        const int pillW = (badgeRow.getWidth() - 2 * 6) / 3;
        auto bpm = badgeRow.removeFromLeft(pillW); badgeRow.removeFromLeft(6);
        auto key = badgeRow.removeFromLeft(pillW); badgeRow.removeFromLeft(6);
        auto nrg = badgeRow;

        drawPill(bpm, juce::Colour(0xFF00D9FF),
                 juce::String(nowPlaying_.bpm, 1) + " BPM");
        drawPill(key, juce::Colour(0xFF22C55E),
                 nowPlaying_.key.isNotEmpty() ? nowPlaying_.key : juce::String("?"));
        drawPill(nrg, juce::Colour(0xFFFACC15),
                 juce::String("E ") + juce::String(nowPlaying_.energy, 1));

        return;
    }

    auto area = getLocalBounds().reduced(28, 8);

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText(nowPlaying_.title, area.removeFromTop(16), juce::Justification::centredLeft);

    g.setColour(Colors::textSecondary());
    g.setFont(juce::Font(10.0f));
    g.drawText(nowPlaying_.artist, area.removeFromTop(14), juce::Justification::centredLeft);

    area.removeFromTop(4);

    auto badgeRow = area.removeFromTop(20);
    {
        auto bpmB = juce::Rectangle<float>(static_cast<float>(badgeRow.getX()), static_cast<float>(badgeRow.getY()),
                                            60.0f, 18.0f);
        g.setColour(juce::Colour(0xFF00D9FF).withAlpha(0.15f));
        g.fillRoundedRectangle(bpmB, 9.0f);
        g.setColour(juce::Colour(0xFF00D9FF));
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText(juce::String(nowPlaying_.bpm, 0) + " BPM", bpmB, juce::Justification::centred);

        auto keyB = juce::Rectangle<float>(bpmB.getRight() + 6.0f, static_cast<float>(badgeRow.getY()),
                                            36.0f, 18.0f);
        g.setColour(Colors::success().withAlpha(0.15f));
        g.fillRoundedRectangle(keyB, 9.0f);
        g.setColour(Colors::success());
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText(nowPlaying_.key, keyB, juce::Justification::centred);

        auto eB = juce::Rectangle<float>(keyB.getRight() + 6.0f, static_cast<float>(badgeRow.getY()),
                                          40.0f, 18.0f);
        g.setColour(Colors::warning().withAlpha(0.15f));
        g.fillRoundedRectangle(eB, 9.0f);
        g.setColour(Colors::warning());
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText("E:" + juce::String(nowPlaying_.energy, 0), eB, juce::Justification::centred);
    }

    area.removeFromTop(6);

    g.setColour(Colors::textDim());
    g.setFont(juce::Font(8.0f, juce::Font::bold));
    g.drawText(BM_TJ("live.suggestionsUpper"), area.removeFromTop(12), juce::Justification::centredLeft);

    int count = juce::jmin(3, static_cast<int>(suggestions_.size()));
    for (int i = 0; i < count; ++i)
    {
        auto row = area.removeFromTop(22);
        const auto& s = suggestions_[static_cast<size_t>(i)];

        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.setFont(juce::Font(9.0f));
        g.drawText(s.title + " - " + s.artist, row.removeFromLeft(row.getWidth() - 40),
                   juce::Justification::centredLeft);

        g.setColour(Colors::textMuted());
        g.setFont(juce::Font(8.0f, juce::Font::bold));
        g.drawText(juce::String(static_cast<int>(s.score * 100)) + "%", row,
                   juce::Justification::centredRight);
    }
}


void BeatMateLiveView::resized()
{
    if (compactMode_)
    {
        if (smartPanel_)         smartPanel_->setVisible(false);
        if (rightTabs_)          rightTabs_->setVisible(false);
        if (crowdEnergyMeter_)   crowdEnergyMeter_->setVisible(false);
        alwaysOnTopToggle_->setVisible(false);
        compactToggle_->setVisible(false);
        settingsButton_->setVisible(false);
        helpButton_->setVisible(false);
        if (importRekordboxXmlButton_) importRekordboxXmlButton_->setVisible(false);
        if (connectButton_)    connectButton_->setVisible(false);
        if (disconnectButton_) disconnectButton_->setVisible(false);
        sourceSelector_->setVisible(false);
        if (profileLbl_)        profileLbl_->setVisible(false);
        if (profileCb_)         profileCb_->setVisible(false);
        if (profileSaveAsBtn_)  profileSaveAsBtn_->setVisible(false);
        tabSuggestions_->setVisible(false);
        tabTrending_->setVisible(false);
        tabHistory_->setVisible(false);
        suggestionList_->setVisible(false);
        trendingList_->setVisible(false);
        historyList_->setVisible(false);
        genreFilter_->setVisible(false);
        countryFilter_->setVisible(false);

        compactBackButton_->setVisible(true);
        auto area = getLocalBounds().reduced(6);
        compactBackButton_->setBounds(area.removeFromBottom(22).reduced(2, 0));
        return;
    }

    compactBackButton_->setVisible(false);

    alwaysOnTopToggle_->setVisible(true);
    compactToggle_->setVisible(true);
    settingsButton_->setVisible(true);
    helpButton_->setVisible(true);
    if (importRekordboxXmlButton_) importRekordboxXmlButton_->setVisible(true);
    sourceSelector_->setVisible(true);
    if (rightTabs_) rightTabs_->setVisible(true);
    if (crowdEnergyMeter_) crowdEnergyMeter_->setVisible(true);

    const bool showSmart = (!cabinMode_
                            && smartEngine_ != nullptr && smartPanel_ != nullptr);
    if (smartPanel_)     smartPanel_->setVisible(showSmart);
    if (suggestionList_) suggestionList_->setVisible(!showSmart);
    if (historyList_)    historyList_->setVisible(true);
    if (trendingList_)   trendingList_->setVisible(true);
    if (genreFilter_)    genreFilter_->setVisible(true);
    if (countryFilter_)  countryFilter_->setVisible(true);
    if (tabSuggestions_) tabSuggestions_->setVisible(false);
    if (tabTrending_)    tabTrending_->setVisible(false);
    if (tabHistory_)     tabHistory_->setVisible(false);

    auto area = getLocalBounds().reduced(12);

    auto headerArea = area.removeFromTop(48);

    const int titleW = 200;
    headerArea.removeFromLeft(titleW);
    headerArea.removeFromLeft(8);

    const int btnH = 32;

    auto placeRight = [&](juce::Component* c, int w)
    {
        if (!c) return;
        auto r = headerArea.removeFromRight(w);
        c->setBounds(r.withSizeKeepingCentre(w, btnH));
        headerArea.removeFromRight(6);
    };

    if (importRekordboxXmlButton_) importRekordboxXmlButton_->setVisible(false);

    if (helpButton_) {
        helpButton_->setVisible(true);
        placeRight(helpButton_.get(), 32);
    }
    if (settingsButton_) {
        settingsButton_->setVisible(true);
        placeRight(settingsButton_.get(), 90);
    }

    if (disconnectButton_ && disconnectButton_->isVisible())
    {
        const int w = measureButtonWidth(BM_TJ("live.disconnect").toStdString().c_str(), 140);
        placeRight(disconnectButton_.get(), w);
    }
    else if (connectButton_)
    {
        connectButton_->setVisible(true);
        const int w = measureButtonWidth("Connecter VirtualDJ", 180);
        placeRight(connectButton_.get(), w);
    }

    if (profileLbl_) profileLbl_->setVisible(false);

    placeRight(sourceSelector_.get(), 130);

    if (compactToggle_)     compactToggle_->setVisible(false);
    if (alwaysOnTopToggle_) alwaysOnTopToggle_->setVisible(false);

    area.removeFromTop(8);

    const bool narrow = area.getWidth() < 780;
    auto body = area;

    if (!narrow)
    {
        const int leftW = juce::jmax(280, static_cast<int>(body.getWidth() * 0.35f));
        auto leftCol = body.removeFromLeft(leftW);
        body.removeFromLeft(8);

        (void)leftCol;

        if (crowdEnergyMeter_) {
            auto strip = body.removeFromTop(96);
            crowdEnergyMeter_->setBounds(strip);
            body.removeFromTop(6);
        }
        if (rightTabs_)
            rightTabs_->setBounds(body);
    }
    else
    {
        auto npArea = body.removeFromTop(200);
        (void)npArea;
        body.removeFromTop(8);

        if (crowdEnergyMeter_) {
            auto strip = body.removeFromTop(96);
            crowdEnergyMeter_->setBounds(strip);
            body.removeFromTop(6);
        }
        if (rightTabs_)
            rightTabs_->setBounds(body);
    }
}

void BeatMateLiveView::showSendToDJMenu(int rowIndex, juce::Point<int> /*screenPos*/)
{
    if (rowIndex < 0 || rowIndex >= static_cast<int>(suggestions_.size()))
        return;
    showSendToDJMenuForEntry(suggestions_[static_cast<size_t>(rowIndex)]);
}

void BeatMateLiveView::showSendToDJMenuForEntry(const SuggestionEntry& entry)
{
    const auto query = (entry.artist + " " + entry.title).trim();
    if (query.isEmpty()) return;
    juce::SystemClipboard::copyTextToClipboard(query);
    Utils::showAutoCloseAlert(juce::MessageBoxIconType::InfoIcon,
        BM_TJ("live.copy.done") + " : " + query,
        BM_TJ("live.copy.procedure"), 7000);
    spdlog::info("[Live copy-for-DJ] '{}'", query.toStdString());
}

void BeatMateLiveView::revealFileInExplorer(const juce::String& filePath)
{
    juce::File file(filePath);
    if (file.existsAsFile())
        file.revealToUser();
}

void BeatMateLiveView::importRekordboxXmlFlow()
{
    importRekordboxXmlChooser_ = std::make_unique<juce::FileChooser>(
        BM_TJ("live.selectRekordboxXml"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.xml");

    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;

    importRekordboxXmlChooser_->launchAsync(flags, [this](const juce::FileChooser& fc)
    {
        auto xml = fc.getResult();
        if (xml == juce::File{}) return;

        extern BeatMate::ServiceLocator* g_serviceLocator;
        Services::Rekordbox::RekordboxService* rbSvc = nullptr;
        Services::Library::PlaylistManager*    plMgr = nullptr;
        if (g_serviceLocator)
        {
            rbSvc = g_serviceLocator->tryGet<Services::Rekordbox::RekordboxService>();
            plMgr = g_serviceLocator->tryGet<Services::Library::PlaylistManager>();
        }

        if (rbSvc == nullptr || plMgr == nullptr || m_provider == nullptr)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                BM_TJ("live.importXmlTitle"),
                BM_TJ("live.importXml.svcUnavail"),
                "OK");
            return;
        }

        if (importBusy_->exchange(true))
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                BM_TJ("live.importXmlTitle"),
                juce::String::fromUTF8("Un import est deja en cours. Patientez."),
                "OK");
            return;
        }

        auto progress = std::make_shared<juce::AlertWindow>(
            BM_TJ("live.importXmlTitle"),
            juce::String::fromUTF8("Import de la collection Rekordbox en arriere-plan. Veuillez patienter..."),
            juce::MessageBoxIconType::InfoIcon);
        progress->enterModalState(false, nullptr, false);
        progress->setVisible(true);

        auto provider = m_provider;
        auto busy = importBusy_;
        auto summaryPtr = std::make_shared<Services::Rekordbox::XmlImportSummary>();
        juce::WeakReference<juce::Component> weakSelf { this };

        std::thread([rbSvc, plMgr, provider, xml, summaryPtr, progress, busy, weakSelf]()
        {
            *summaryPtr = rbSvc->importFromXmlFile(xml, plMgr, provider);

            juce::MessageManager::callAsync([summaryPtr, progress, busy, weakSelf, xml]()
            {
                progress->exitModalState(0);
                progress->setVisible(false);
                busy->store(false);

                const auto& summary = *summaryPtr;
                if (!summary.ok())
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        BM_TJ("live.importXmlTitle"),
                        BM_TJ("live.importXml.fail") + "\n" + juce::String(summary.error),
                        "OK");
                    return;
                }

                juce::String msg;
                msg << BM_TJ("live.importXml.done") << "\n\n"
                    << BM_TJ("live.importXml.tracks") << " " << summary.tracksImported << "\n"
                    << BM_TJ("live.importXml.playlists") << " " << summary.playlistsImported << "\n"
                    << BM_TJ("live.importXml.skipped") << " " << summary.skipped;

                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::InfoIcon,
                    BM_TJ("live.importXmlTitle"),
                    msg,
                    "OK");
            });
        }).detach();
    });
}

} // namespace BeatMate::UI
