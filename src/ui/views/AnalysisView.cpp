#include "AnalysisView.h"
#include "../styles/ColorPalette.h"
#include "../../services/library/TrackDataProvider.h"
#include "../../services/config/I18n.h"
#include "../utils/ViewPrefs.h"
#include "../widgets/ToastNotifier.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace BeatMate::UI {

namespace {
constexpr int kMargin = 24;
constexpr int kTitleY = 16;
constexpr int kToolbarY = 56;
constexpr int kToolbarH = 44;
constexpr int kOptionsY = 106;
constexpr int kOptionsH = 38;
constexpr int kActionsY = 152;
constexpr int kActionsH = 36;
constexpr int kCardY = 196;
constexpr int kDetailW = 360;
constexpr int kDetailMinViewW = 1080;
constexpr int kRowHeight = 34;
}

void AnalysisView::TrackModel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= static_cast<int>(visible.size()))
        return;
    auto& e = view.m_allTracks[static_cast<size_t>(visible[static_cast<size_t>(row)])];

    if (selected) {
        g.setColour(Colors::primary().withAlpha(0.12f));
        g.fillRect(0, 0, w, h);
        g.setColour(Colors::primary().withAlpha(0.85f));
        g.fillRect(0, 2, 3, h - 4);
    } else if (row % 2 == 1) {
        g.setColour(juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.02f));
        g.fillRect(0, 0, w, h);
    }

    using AnalysisColumns::Col;
    auto bounds = [&](Col c) { return AnalysisColumns::columnBounds(c, w, h); };

    {
        auto b = bounds(Col::Check);
        const float cbSize = 14.0f;
        const float cbX = b.getCentreX() - cbSize * 0.5f;
        const float cbY = b.getCentreY() - cbSize * 0.5f;
        if (e.checked) {
            g.setColour(Colors::primary());
            g.fillRoundedRectangle(cbX, cbY, cbSize, cbSize, 3.0f);
            g.setColour(Colors::bg());
            g.setFont(juce::Font(11.0f, juce::Font::bold));
            g.drawText(juce::String::charToString(static_cast<juce::juce_wchar>(0x2713)),
                       static_cast<int>(cbX), static_cast<int>(cbY),
                       static_cast<int>(cbSize), static_cast<int>(cbSize),
                       juce::Justification::centred);
        } else {
            g.setColour(Colors::border());
            g.drawRoundedRectangle(cbX, cbY, cbSize, cbSize, 3.0f, 1.5f);
        }
    }

    g.setFont(juce::Font(12.5f));
    g.setColour(e.checked || selected ? Colors::textPrimary() : Colors::textSecondary());
    g.drawText(e.title, bounds(Col::Title).reduced(8, 0), juce::Justification::centredLeft, true);
    g.setColour(Colors::textSecondary());
    g.drawText(e.artist, bounds(Col::Artist).reduced(8, 0), juce::Justification::centredLeft, true);

    {
        auto b = bounds(Col::Status);
        juce::Colour c;
        juce::String txt;
        bool pulse = false;
        switch (e.status) {
            case RowStatus::Running: c = Colors::primary(); txt = BM_TJ("analysis.row.running"); pulse = true; break;
            case RowStatus::Done:    c = Colors::success(); txt = BM_TJ("analysis.row.done"); break;
            case RowStatus::Error:   c = Colors::error();   txt = BM_TJ("analysis.row.error"); break;
            case RowStatus::Pending:
            default:                 c = Colors::warning(); txt = BM_TJ("analysis.row.pending"); break;
        }
        ProDraw::statusPill(g, txt, b.toFloat().reduced(6.0f, 7.0f), c, pulse);
    }

    {
        auto b = bounds(Col::Bpm);
        if (e.bpmValue > 0.0) {
            g.setColour(Colors::textPrimary());
            g.setFont(juce::FontOptions("Consolas", 12.0f, juce::Font::plain));
            g.drawText(juce::String(e.bpmValue, 1), b.reduced(8, 0).withTrimmedRight(10),
                       juce::Justification::centred);
            if (e.bpmConfidence > 0.0f) {
                const juce::Colour dot = e.bpmConfidence >= 0.75f ? Colors::success()
                                       : e.bpmConfidence >= 0.45f ? Colors::warning()
                                                                  : Colors::error();
                g.setColour(dot);
                g.fillEllipse(static_cast<float>(b.getRight() - 16),
                              static_cast<float>(b.getCentreY() - 2), 5.0f, 5.0f);
            }
        } else {
            g.setColour(Colors::textDim());
            g.setFont(juce::Font(12.0f));
            g.drawText(juce::String::fromUTF8("\xe2\x80\x94"), b, juce::Justification::centred);
        }
    }

    {
        auto b = bounds(Col::Key);
        if (e.camelot.isNotEmpty()) {
            ProDraw::badge(g, e.camelot, static_cast<float>(b.getCentreX() - 22),
                           static_cast<float>(b.getCentreY() - 9), 44.0f, 18.0f,
                           AnalysisColumns::camelotColour(e.camelot));
        } else if (e.key.isNotEmpty()) {
            g.setColour(Colors::keyBadge());
            g.setFont(juce::Font(12.0f, juce::Font::bold));
            g.drawText(e.key, b, juce::Justification::centred);
        } else {
            g.setColour(Colors::textDim());
            g.setFont(juce::Font(12.0f));
            g.drawText(juce::String::fromUTF8("\xe2\x80\x94"), b, juce::Justification::centred);
        }
    }

    {
        auto b = bounds(Col::Energy);
        if (!e.sparkParsed) {
            e.sparkParsed = true;
            e.spark.clear();
            if (e.energySegmentsJson.isNotEmpty()) {
                try {
                    auto j = nlohmann::json::parse(e.energySegmentsJson.toStdString());
                    for (const auto& seg : j)
                        e.spark.push_back(static_cast<float>(seg.value("energy", 0)) / 10.0f);
                } catch (...) {
                    e.spark.clear();
                }
            }
        }
        if (e.spark.size() >= 2) {
            const int maxBars = 14;
            const int n = juce::jmin(maxBars, static_cast<int>(e.spark.size()));
            const float areaW = 64.0f;
            const float barW = areaW / static_cast<float>(n);
            const float x0 = static_cast<float>(b.getCentreX()) - areaW * 0.5f;
            const float maxH = static_cast<float>(h) - 14.0f;
            const size_t stride = e.spark.size() / static_cast<size_t>(n);
            for (int i = 0; i < n; ++i) {
                const float v = juce::jlimit(0.05f, 1.0f,
                    e.spark[juce::jmin(e.spark.size() - 1, static_cast<size_t>(i) * stride)]);
                const float bh = juce::jmax(2.0f, maxH * v);
                g.setColour(AnalysisColumns::energyColour(v).withAlpha(0.85f));
                g.fillRoundedRectangle(x0 + static_cast<float>(i) * barW,
                                       static_cast<float>(h) - 7.0f - bh,
                                       juce::jmax(1.5f, barW - 1.5f), bh, 1.0f);
            }
        } else if (e.energy > 0.0f) {
            ProDraw::badge(g, juce::String(static_cast<int>(e.energy + 0.5f)),
                           static_cast<float>(b.getCentreX() - 14),
                           static_cast<float>(b.getCentreY() - 9), 28.0f, 18.0f,
                           AnalysisColumns::energyColour(e.energy / 10.0f));
        } else {
            g.setColour(Colors::textDim());
            g.setFont(juce::Font(12.0f));
            g.drawText(juce::String::fromUTF8("\xe2\x80\x94"), b, juce::Justification::centred);
        }
    }

    {
        auto b = bounds(Col::Lufs);
        if (e.lufs != 0.0f) {
            g.setColour(Colors::textSecondary());
            g.setFont(juce::FontOptions("Consolas", 11.0f, juce::Font::plain));
            g.drawText(juce::String(e.lufs, 1), b, juce::Justification::centred);
        } else {
            g.setColour(Colors::textDim());
            g.setFont(juce::Font(12.0f));
            g.drawText(juce::String::fromUTF8("\xe2\x80\x94"), b, juce::Justification::centred);
        }
    }

    {
        auto b = bounds(Col::Progress);
        if (e.status == RowStatus::Running) {
            const float barW = 56.0f;
            const float barX = static_cast<float>(b.getCentreX()) - barW * 0.5f;
            const float barY = static_cast<float>(b.getCentreY()) - 3.0f;
            g.setColour(Colors::bgElevated());
            g.fillRoundedRectangle(barX, barY, barW, 6.0f, 3.0f);
            juce::ColourGradient grad(Colors::primary(), barX, barY,
                                      Colors::secondary(), barX + barW, barY, false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(barX, barY, barW * juce::jlimit(0, 100, e.progress) / 100.0f,
                                   6.0f, 3.0f);
        } else if (e.status == RowStatus::Done) {
            g.setColour(Colors::success());
            g.setFont(juce::Font(13.0f, juce::Font::bold));
            g.drawText(juce::CharPointer_UTF8("\xe2\x9c\x93"), b, juce::Justification::centred);
        } else if (e.status == RowStatus::Error) {
            g.setColour(Colors::error());
            g.setFont(juce::Font(13.0f, juce::Font::bold));
            g.drawText(juce::CharPointer_UTF8("\xe2\x9c\x95"), b, juce::Justification::centred);
        } else {
            g.setColour(Colors::textDim());
            g.setFont(juce::Font(12.0f));
            g.drawText(juce::String::fromUTF8("\xe2\x80\x94"), b, juce::Justification::centred);
        }
    }

    g.setColour(Colors::glassBorder());
    g.drawHorizontalLine(h - 1, 0.0f, static_cast<float>(w));
}

void AnalysisView::TrackModel::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= static_cast<int>(visible.size()))
        return;
    const int rowW = view.m_trackList ? view.m_trackList->getVisibleRowWidth() : 0;
    const auto col = AnalysisColumns::columnAt(e.x, rowW);
    if (col == AnalysisColumns::Col::Check) {
        auto& entry = view.m_allTracks[static_cast<size_t>(visible[static_cast<size_t>(row)])];
        entry.checked = !entry.checked;
        view.updateSelectAllLabel();
        if (view.m_trackList)
            view.m_trackList->repaintRow(row);
    }
}

void AnalysisView::TrackModel::selectedRowsChanged(int lastRowSelected)
{
    if (lastRowSelected >= 0 && lastRowSelected < static_cast<int>(visible.size()))
        view.showDetailForEntry(visible[static_cast<size_t>(lastRowSelected)]);
}

AnalysisView::AnalysisView() : m_provider(nullptr) { setupUI(); retranslateUi(); }

AnalysisView::AnalysisView(Services::Library::TrackDataProvider* provider) : m_provider(provider)
{
    setupUI();
    if (m_provider) {
        loadAllFromDatabase();
        populateGenreFilter();
        applyFilters();
        juce::Component::SafePointer<AnalysisView> self(this);
        m_provider->onDataChanged([self] {
            juce::MessageManager::callAsync([self] {
                if (!self) return;
                self->loadAllFromDatabase();
                self->populateGenreFilter();
                self->applyFilters();
            });
        });
    }
    retranslateUi();
}

AnalysisView::~AnalysisView()
{
    stopTimer();
    m_integrityCancel.store(true);
    joinIntegrityWorkers();
}

void AnalysisView::retranslateUi()
{
    auto rebuild = [](juce::ComboBox* cb, std::initializer_list<const char*> keys) {
        if (!cb) return;
        int prev = cb->getSelectedId();
        cb->clear(juce::dontSendNotification);
        int id = 1;
        for (auto k : keys) cb->addItem(BM_TJ(k), id++);
        if (prev > 0)
            cb->setSelectedId(prev, juce::dontSendNotification);
    };

    if (m_titleLabel) m_titleLabel->setText(BM_TJ("analysis.title"), juce::dontSendNotification);

    if (m_searchLabel) m_searchLabel->setText(BM_TJ("analysis.filter.search"), juce::dontSendNotification);
    if (m_searchEditor) m_searchEditor->setTextToShowWhenEmpty(BM_TJ("analysis.filter.searchPH"), Colors::textMuted());

    if (m_genreLabel) m_genreLabel->setText(BM_TJ("analysis.filter.genre"), juce::dontSendNotification);

    if (m_statusFilterLabel) m_statusFilterLabel->setText(BM_TJ("analysis.filter.status"), juce::dontSendNotification);
    rebuild(m_statusFilter.get(), { "common.all",
                                    "analysis.filter.statusNotAnalyzed",
                                    "analysis.filter.statusAnalyzed",
                                    "analysis.filter.statusError" });

    if (m_bpmMinSlider) m_bpmMinSlider->setTooltip(BM_TJ("analysis.settings.bpmRangeTip"));
    if (m_bpmMaxSlider) m_bpmMaxSlider->setTooltip(BM_TJ("analysis.settings.bpmRangeTip"));

    if (m_optKey)       m_optKey->setButtonText(BM_TJ("analysis.opt.key"));
    if (m_optEnergy)    m_optEnergy->setButtonText(BM_TJ("analysis.opt.energy"));
    if (m_optStructure) m_optStructure->setButtonText(BM_TJ("analysis.opt.structure"));
    if (m_optWaveform)  m_optWaveform->setButtonText(BM_TJ("analysis.opt.waveform"));
    if (m_optMood)      m_optMood->setButtonText(BM_TJ("analysis.opt.mood"));

    if (m_addTracksBtn)       m_addTracksBtn->setButtonText(BM_TJ("analysis.btn.addTracks"));
    if (m_clearBtn)           m_clearBtn->setButtonText(BM_TJ("analysis.btn.clear"));
    if (m_analyzeAllBtn)      m_analyzeAllBtn->setText(BM_TJ("analysis.btn.analyzeAll"));
    if (m_analyzeSelectedBtn) m_analyzeSelectedBtn->setButtonText(BM_TJ("analysis.btn.analyzeSelected"));
    if (m_cancelBtn)          m_cancelBtn->setButtonText(m_analyzing && !m_cancelBtn->isEnabled()
                                                             ? BM_TJ("analysis.status.cancelling")
                                                             : BM_TJ("common.cancel"));

    updateSelectAllLabel();
    refreshCountLabel();

    if (m_listHeader) m_listHeader->retranslateUi();
    if (m_progressCard) m_progressCard->retranslateUi();
    if (m_detailPanel) m_detailPanel->retranslateUi();

    repaint();
}

void AnalysisView::loadAllFromDatabase()
{
    if (!m_provider) return;
    m_allTracks.clear();

    auto allTracks = m_provider->getAllTracks();
    m_allTracks.reserve(allTracks.size());
    m_statTotal = static_cast<int>(allTracks.size());
    m_statAnalyzed = 0;

    for (auto& t : allTracks) {
        FullTrackEntry fe;
        fe.id = t.id;
        fe.title = juce::String::fromUTF8(t.title.c_str());
        fe.artist = juce::String::fromUTF8(t.artist.c_str());
        fe.genre = juce::String::fromUTF8(t.genre.c_str());
        fe.path = juce::String(t.filePath);
        fe.bpmValue = t.bpm;
        fe.bpmConfidence = t.bpmConfidence;
        fe.analyzed = t.analyzed;
        fe.status = t.analyzed ? RowStatus::Done : RowStatus::Pending;
        fe.progress = t.analyzed ? 100 : 0;
        fe.energy = t.energy;
        fe.lufs = t.lufs;
        fe.energySegmentsJson = juce::String(t.energySegments);
        if (t.analyzed) {
            ++m_statAnalyzed;
            fe.key = t.key.empty() ? juce::String() : juce::String(t.key);
            const juce::String cam(t.camelotKey);
            const int num = cam.initialSectionContainingOnly("0123456789").getIntValue();
            if (num >= 1 && num <= 12
                && (cam.endsWithIgnoreCase("A") || cam.endsWithIgnoreCase("B")))
                fe.camelot = cam.toUpperCase();
        }
        m_allTracks.push_back(std::move(fe));
    }
}

void AnalysisView::populateGenreFilter()
{
    if (!m_provider || !m_genreFilter) return;

    int currentId = m_genreFilter->getSelectedId();
    m_genreFilter->clear(juce::dontSendNotification);
    m_genreFilter->addItem(BM_TJ("common.all"), 1);

    auto genres = m_provider->getGenreDistribution();
    int id = 2;
    for (auto& g : genres)
        if (!g.first.empty())
            m_genreFilter->addItem(juce::String::fromUTF8(g.first.c_str()), id++);

    m_genreFilter->setSelectedId(currentId > 0 ? currentId : 1, juce::dontSendNotification);
}

void AnalysisView::applyFilters()
{
    m_trackModel->visible.clear();

    juce::String searchText = m_searchEditor ? m_searchEditor->getText().trim().toLowerCase() : juce::String();
    int genreId = m_genreFilter ? m_genreFilter->getSelectedId() : 1;
    juce::String genreText;
    if (genreId > 1 && m_genreFilter)
        genreText = m_genreFilter->getItemText(m_genreFilter->indexOfItemId(genreId));

    int statusId = m_statusFilter ? m_statusFilter->getSelectedId() : 1;
    double bpmMin = m_bpmMinSlider ? m_bpmMinSlider->getValue() : 60.0;
    double bpmMax = m_bpmMaxSlider ? m_bpmMaxSlider->getValue() : 200.0;

    for (int i = 0; i < static_cast<int>(m_allTracks.size()); ++i) {
        const auto& ft = m_allTracks[static_cast<size_t>(i)];
        if (searchText.isNotEmpty()
            && !ft.title.toLowerCase().contains(searchText)
            && !ft.artist.toLowerCase().contains(searchText))
            continue;
        if (genreText.isNotEmpty() && !ft.genre.equalsIgnoreCase(genreText))
            continue;
        if (statusId == 2 && ft.analyzed) continue;
        if (statusId == 3 && !ft.analyzed) continue;
        if (statusId == 4 && ft.status != RowStatus::Error) continue;
        if (ft.bpmValue > 0.0 && (ft.bpmValue < bpmMin || ft.bpmValue > bpmMax))
            continue;
        m_trackModel->visible.push_back(i);
    }

    sortByColumn(m_sortColumn);
    m_trackList->deselectAllRows();
    m_trackList->updateContent();
    refreshCountLabel();
    updateSelectAllLabel();
    repaint();
}

void AnalysisView::refreshCountLabel()
{
    if (m_trackCountLabel && m_trackModel)
        m_trackCountLabel->setText(juce::String(m_trackModel->visible.size()) + " "
                                       + BM_TJ("analysis.stats.tracks"),
                                   juce::dontSendNotification);
}

void AnalysisView::sortByColumn(AnalysisColumns::Col col)
{
    using AnalysisColumns::Col;
    auto& vis = m_trackModel->visible;
    std::stable_sort(vis.begin(), vis.end(), [this, col](int ia, int ib) {
        const auto& a = m_allTracks[static_cast<size_t>(ia)];
        const auto& b = m_allTracks[static_cast<size_t>(ib)];
        int cmp = 0;
        switch (col) {
            case Col::Title:  cmp = a.title.compareIgnoreCase(b.title); break;
            case Col::Artist: cmp = a.artist.compareIgnoreCase(b.artist); break;
            case Col::Status: cmp = static_cast<int>(a.status) - static_cast<int>(b.status); break;
            case Col::Bpm:    cmp = (a.bpmValue < b.bpmValue) ? -1 : (a.bpmValue > b.bpmValue ? 1 : 0); break;
            case Col::Key:    cmp = (a.camelot.isNotEmpty() ? a.camelot : a.key)
                                        .compareIgnoreCase(b.camelot.isNotEmpty() ? b.camelot : b.key); break;
            case Col::Energy: cmp = (a.energy < b.energy) ? -1 : (a.energy > b.energy ? 1 : 0); break;
            case Col::Lufs:   cmp = (a.lufs < b.lufs) ? -1 : (a.lufs > b.lufs ? 1 : 0); break;
            case Col::Progress: cmp = a.progress - b.progress; break;
            default: break;
        }
        return m_sortAscending ? (cmp < 0) : (cmp > 0);
    });

    if (m_listHeader)
        m_listHeader->setSortState(m_sortColumn, m_sortAscending);
    m_trackList->updateContent();
    m_trackList->repaint();
}

int AnalysisView::allIndexForPath(const juce::String& path) const
{
    for (int i = 0; i < static_cast<int>(m_allTracks.size()); ++i)
        if (m_allTracks[static_cast<size_t>(i)].path == path)
            return i;
    return -1;
}

void AnalysisView::updateSelectAllLabel()
{
    if (!m_selectAllBtn || !m_trackModel)
        return;
    int checkedVisible = 0;
    for (int idx : m_trackModel->visible)
        if (m_allTracks[static_cast<size_t>(idx)].checked)
            ++checkedVisible;
    const bool allChecked = !m_trackModel->visible.empty()
        && checkedVisible == static_cast<int>(m_trackModel->visible.size());
    m_selectAllBtn->setButtonText(allChecked ? BM_TJ("analysis.settings.deselectAll")
                                             : BM_TJ("analysis.btn.selectAll"));
}

void AnalysisView::showDetailForEntry(int allIndex)
{
    if (!m_detailPanel || allIndex < 0 || allIndex >= static_cast<int>(m_allTracks.size()))
        return;
    const auto& e = m_allTracks[static_cast<size_t>(allIndex)];
    if (m_provider) {
        auto track = m_provider->getTrack(e.id);
        if (track.id != 0) {
            m_detailPanel->setTrack(track);
            return;
        }
    }
    Models::Track t;
    t.id = e.id;
    t.title = e.title.toStdString();
    t.artist = e.artist.toStdString();
    t.filePath = e.path.toStdString();
    t.bpm = e.bpmValue;
    t.key = e.key.toStdString();
    t.camelotKey = e.camelot.toStdString();
    t.energy = e.energy;
    t.lufs = e.lufs;
    t.analyzed = e.analyzed;
    t.energySegments = e.energySegmentsJson.toStdString();
    m_detailPanel->setTrack(t);
}

void AnalysisView::setupUI()
{
    m_titleLabel = std::make_unique<juce::Label>("t", BM_TJ("analysis.title"));
    m_titleLabel->setFont(Type::display());
    m_titleLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    auto makeLbl = [this](const juce::String& text) {
        auto lbl = std::make_unique<juce::Label>("", text);
        lbl->setFont(juce::Font(11.0f));
        lbl->setColour(juce::Label::textColourId, Colors::textMuted());
        addAndMakeVisible(*lbl);
        return lbl;
    };

    m_searchLabel = makeLbl(BM_TJ("analysis.filter.search"));
    m_searchEditor = std::make_unique<juce::TextEditor>("search");
    m_searchEditor->setTextToShowWhenEmpty(BM_TJ("analysis.filter.searchPH"), Colors::textMuted());
    m_searchEditor->setColour(juce::TextEditor::backgroundColourId, Colors::bgCard());
    m_searchEditor->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
    m_searchEditor->setColour(juce::TextEditor::outlineColourId, Colors::border());
    m_searchEditor->onTextChange = [this] { applyFilters(); };
    addAndMakeVisible(*m_searchEditor);

    m_genreLabel = makeLbl(BM_TJ("analysis.filter.genre"));
    m_genreFilter = std::make_unique<juce::ComboBox>("genreFilter");
    m_genreFilter->addItem(BM_TJ("common.all"), 1);
    m_genreFilter->setSelectedId(1, juce::dontSendNotification);
    m_genreFilter->setColour(juce::ComboBox::backgroundColourId, Colors::bgCard());
    m_genreFilter->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_genreFilter->setColour(juce::ComboBox::outlineColourId, Colors::border());
    m_genreFilter->onChange = [this] {
        Prefs::setInt("analysis.genreFilterId", m_genreFilter->getSelectedId());
        Prefs::setString("analysis.genreFilterText", m_genreFilter->getText().toStdString());
        applyFilters();
    };
    addAndMakeVisible(*m_genreFilter);

    m_statusFilterLabel = makeLbl(BM_TJ("analysis.filter.status"));
    m_statusFilter = std::make_unique<juce::ComboBox>("statusFilter");
    m_statusFilter->addItem(BM_TJ("common.all"), 1);
    m_statusFilter->addItem(BM_TJ("analysis.filter.statusNotAnalyzed"), 2);
    m_statusFilter->addItem(BM_TJ("analysis.filter.statusAnalyzed"), 3);
    m_statusFilter->addItem(BM_TJ("analysis.filter.statusError"), 4);
    m_statusFilter->setSelectedId(2, juce::dontSendNotification);
    m_statusFilter->setColour(juce::ComboBox::backgroundColourId, Colors::bgCard());
    m_statusFilter->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_statusFilter->setColour(juce::ComboBox::outlineColourId, Colors::border());
    m_statusFilter->onChange = [this] {
        Prefs::setInt("analysis.statusFilterId", m_statusFilter->getSelectedId());
        applyFilters();
    };
    addAndMakeVisible(*m_statusFilter);

    m_bpmRangeLabel = makeLbl(BM_TJ("analysis.filter.bpmRange"));
    m_bpmMinSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    m_bpmMinSlider->setRange(60, 200, 1);
    m_bpmMinSlider->setValue(60, juce::dontSendNotification);
    m_bpmMinSlider->setColour(juce::Slider::thumbColourId, Colors::primary());
    m_bpmMinSlider->setColour(juce::Slider::trackColourId, Colors::bgElevated());
    m_bpmMinSlider->setTooltip(BM_TJ("analysis.settings.bpmRangeTip"));
    m_bpmMinSlider->onValueChange = [this] {
        if (m_bpmMinSlider->getValue() > m_bpmMaxSlider->getValue())
            m_bpmMaxSlider->setValue(m_bpmMinSlider->getValue(), juce::dontSendNotification);
        m_bpmRangeLabel->setText("BPM: " + juce::String((int)m_bpmMinSlider->getValue()) + "-" + juce::String((int)m_bpmMaxSlider->getValue()), juce::dontSendNotification);
        Prefs::setInt("analysis.bpmMin", (int)m_bpmMinSlider->getValue());
        Prefs::setInt("analysis.bpmMax", (int)m_bpmMaxSlider->getValue());
        applyFilters();
    };
    addAndMakeVisible(*m_bpmMinSlider);

    m_bpmMaxSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    m_bpmMaxSlider->setRange(60, 200, 1);
    m_bpmMaxSlider->setValue(200, juce::dontSendNotification);
    m_bpmMaxSlider->setColour(juce::Slider::thumbColourId, Colors::accent());
    m_bpmMaxSlider->setColour(juce::Slider::trackColourId, Colors::bgElevated());
    m_bpmMaxSlider->setTooltip(BM_TJ("analysis.settings.bpmRangeTip"));
    m_bpmMaxSlider->onValueChange = [this] {
        if (m_bpmMaxSlider->getValue() < m_bpmMinSlider->getValue())
            m_bpmMinSlider->setValue(m_bpmMaxSlider->getValue(), juce::dontSendNotification);
        m_bpmRangeLabel->setText("BPM: " + juce::String((int)m_bpmMinSlider->getValue()) + "-" + juce::String((int)m_bpmMaxSlider->getValue()), juce::dontSendNotification);
        Prefs::setInt("analysis.bpmMin", (int)m_bpmMinSlider->getValue());
        Prefs::setInt("analysis.bpmMax", (int)m_bpmMaxSlider->getValue());
        applyFilters();
    };
    addAndMakeVisible(*m_bpmMaxSlider);

    auto makeOpt = [this](const juce::String& text, bool checked = true) {
        auto btn = std::make_unique<juce::ToggleButton>(text);
        btn->setToggleState(checked, juce::dontSendNotification);
        btn->setColour(juce::ToggleButton::textColourId, Colors::textSecondary());
        btn->setColour(juce::ToggleButton::tickColourId, Colors::primary());
        addAndMakeVisible(*btn);
        return btn;
    };
    auto bindOpt = [](juce::ToggleButton* b, const std::string& key, bool defaultVal) {
        const bool v = Prefs::getBool(key, defaultVal);
        b->setToggleState(v, juce::dontSendNotification);
        b->onClick = [b, key] { Prefs::setBool(key, b->getToggleState()); };
    };
    m_optBPM = makeOpt("BPM");
    m_optKey = makeOpt(BM_TJ("analysis.opt.key"));
    m_optEnergy = makeOpt(BM_TJ("analysis.opt.energy"));
    m_optStructure = makeOpt(BM_TJ("analysis.opt.structure"));
    m_optWaveform = makeOpt(BM_TJ("analysis.opt.waveform"));
    m_optMood = makeOpt(BM_TJ("analysis.opt.mood"), false);
    m_optUltraStems = makeOpt(juce::String::fromUTF8("Stems Ultra (MDX-GPU)"), false);
    m_optUltraStems->setTooltip(juce::String::fromUTF8(
        "Pré-sépare les stems en haute qualité (GPU) pendant l'analyse, "
        "pour des stems instantanés au Studio. Nécessite le moteur stemsep."));
    bindOpt(m_optBPM.get(),        "analysis.opt.bpm",       true);
    bindOpt(m_optKey.get(),        "analysis.opt.key",       true);
    bindOpt(m_optEnergy.get(),     "analysis.opt.energy",    true);
    bindOpt(m_optStructure.get(),  "analysis.opt.structure", true);
    bindOpt(m_optWaveform.get(),   "analysis.opt.waveform",  true);
    bindOpt(m_optMood.get(),       "analysis.opt.mood",      false);
    bindOpt(m_optUltraStems.get(), "stems_ultra_mdx_gpu",    false);

    auto makeBtn = [this](const juce::String& t, juce::Colour bg = Colors::bgLighter()) {
        auto btn = std::make_unique<juce::TextButton>(t);
        btn->setColour(juce::TextButton::buttonColourId, bg);
        btn->setColour(juce::TextButton::buttonOnColourId, bg.brighter(0.2f));
        btn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        btn->setColour(juce::TextButton::textColourOnId, Colors::textPrimary());
        addAndMakeVisible(*btn);
        return btn;
    };
    m_addTracksBtn = makeBtn(BM_TJ("analysis.btn.addTracks"));
    m_addTracksBtn->onClick = [this] {
        juce::FileChooser chooser(BM_TJ("analysis.chooseTracks"), juce::File{}, "*.mp3;*.wav;*.flac;*.aac;*.ogg;*.m4a;*.aiff");
        if (chooser.browseForMultipleFilesToOpen())
            for (auto& f : chooser.getResults())
                addTrackForAnalysis(f.getFileNameWithoutExtension(), "", f.getFullPathName());
    };
    m_clearBtn = makeBtn(BM_TJ("analysis.btn.clear"));
    m_clearBtn->onClick = [this] { clearTracks(); };

    m_integrity = std::make_unique<Services::Analysis::AudioIntegrityChecker>();
    m_integrityBtn = makeBtn(juce::String::fromUTF8("V\xc3\xa9rifier / R\xc3\xa9parer \xe2\x96\xbe"));
    m_integrityBtn->setTooltip(juce::String::fromUTF8(
        "Contr\xc3\xb4le d'int\xc3\xa9grit\xc3\xa9 des fichiers audio : "
        "d\xc3\xa9tection des fichiers tronqu\xc3\xa9s ou corrompus, et r\xc3\xa9paration sans perte."));
    m_integrityBtn->onClick = [this] { showIntegrityMenu(); };

    m_analyzeAllBtn = std::make_unique<GradientButton>(BM_TJ("analysis.btn.analyzeAll"));
    m_analyzeAllBtn->onClick = [this] {
        spdlog::info("[AnalysisView] Analyser tout clicked");
        beginRunUi();
        m_listeners.call(&Listener::analyzeAllRequested);
    };
    addAndMakeVisible(*m_analyzeAllBtn);

    m_analyzeSelectedBtn = makeBtn(BM_TJ("analysis.btn.analyzeSelected"));
    m_analyzeSelectedBtn->onClick = [this] {
        auto selectedPaths = getSelectedPaths();
        if (selectedPaths.isEmpty()) {
            Widgets::ToastNotifier::getInstance().show(
                BM_TJ("analysis.warn.noSelectionTitle"),
                BM_TJ("analysis.warn.noSelectionBody"),
                Widgets::ToastNotifier::Kind::Warning, 6000);
            return;
        }
        spdlog::info("[AnalysisView] analyzeSelected clicked ({} paths)", selectedPaths.size());
        beginRunUi();
        m_listeners.call([&selectedPaths](Listener& l) { l.analyzeSelectedRequested(selectedPaths); });
    };

    m_cancelBtn = makeBtn(BM_TJ("common.cancel"), Colors::error().withAlpha(0.75f));
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->onClick = [this] {
        spdlog::info("[AnalysisView] cancelAnalysis clicked");
        m_cancelBtn->setEnabled(false);
        m_cancelBtn->setButtonText(BM_TJ("analysis.status.cancelling"));
        m_listeners.call(&Listener::analysisCancelled);
    };

    m_trackCountLabel = std::make_unique<juce::Label>("cnt", "0");
    m_trackCountLabel->setFont(juce::Font(11.0f));
    m_trackCountLabel->setColour(juce::Label::textColourId, Colors::textMuted());
    m_trackCountLabel->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*m_trackCountLabel);

    m_trackModel = std::make_unique<TrackModel>(*this);
    m_trackList = std::make_unique<juce::ListBox>("tracks", m_trackModel.get());
    m_trackList->setRowHeight(kRowHeight);
    m_trackList->setMultipleSelectionEnabled(false);
    m_trackList->setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    m_trackList->setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(*m_trackList);

    m_listHeader = std::make_unique<AnalysisListHeader>();
    m_listHeader->setSortState(m_sortColumn, m_sortAscending);
    m_listHeader->onSortRequested = [this](AnalysisColumns::Col col) {
        if (m_sortColumn == col)
            m_sortAscending = !m_sortAscending;
        else {
            m_sortColumn = col;
            m_sortAscending = true;
        }
        sortByColumn(m_sortColumn);
    };
    addAndMakeVisible(*m_listHeader);

    m_progressCard = std::make_unique<AnalysisProgressCard>();
    addAndMakeVisible(*m_progressCard);

    m_detailPanel = std::make_unique<TrackDetailPanel>();
    addAndMakeVisible(*m_detailPanel);

    m_selectAllBtn = makeBtn(BM_TJ("analysis.btn.selectAll"));
    m_selectAllBtn->onClick = [this] {
        int checkedVisible = 0;
        for (int idx : m_trackModel->visible)
            if (m_allTracks[static_cast<size_t>(idx)].checked)
                ++checkedVisible;
        const bool allChecked = !m_trackModel->visible.empty()
            && checkedVisible == static_cast<int>(m_trackModel->visible.size());
        for (int idx : m_trackModel->visible)
            m_allTracks[static_cast<size_t>(idx)].checked = !allChecked;
        updateSelectAllLabel();
        m_trackList->repaint();
    };

    {
        const int bpmMin = Prefs::getInt("analysis.bpmMin", 60);
        const int bpmMax = Prefs::getInt("analysis.bpmMax", 200);
        m_bpmMinSlider->setValue(bpmMin, juce::dontSendNotification);
        m_bpmMaxSlider->setValue(bpmMax, juce::dontSendNotification);
        m_bpmRangeLabel->setText("BPM: " + juce::String(bpmMin) + "-" + juce::String(bpmMax),
                                 juce::dontSendNotification);
        const int statusId = Prefs::getInt("analysis.statusFilterId", 2);
        if (statusId >= 1 && statusId <= m_statusFilter->getNumItems())
            m_statusFilter->setSelectedId(statusId, juce::dontSendNotification);
    }
}

void AnalysisView::addTrackForAnalysis(const juce::String& title, const juce::String& artist, const juce::String& path)
{
    FullTrackEntry fe;
    fe.title = title;
    fe.artist = artist;
    fe.path = path;
    m_allTracks.push_back(std::move(fe));
    ++m_statTotal;
    applyFilters();
}

void AnalysisView::clearTracks()
{
    m_allTracks.clear();
    m_trackModel->visible.clear();
    m_trackList->updateContent();
    m_statTotal = 0;
    m_statAnalyzed = 0;
    m_analyzing = false;
    if (m_detailPanel)
        m_detailPanel->clearTrack();
    setCardTarget(0);
    if (m_analyzeAllBtn) m_analyzeAllBtn->setEnabled(true);
    m_analyzeSelectedBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->setButtonText(BM_TJ("common.cancel"));
    refreshCountLabel();
    repaint();
}

juce::StringArray AnalysisView::getSelectedPaths() const
{
    juce::StringArray paths;
    for (int idx : m_trackModel->visible) {
        const auto& e = m_allTracks[static_cast<size_t>(idx)];
        if (e.checked)
            paths.add(e.path);
    }
    return paths;
}

void AnalysisView::beginRunUi()
{
    m_analyzing = true;
    m_analyzeAllBtn->setEnabled(false);
    m_analyzeSelectedBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_cancelBtn->setButtonText(BM_TJ("common.cancel"));
    m_progressCard->beginRun();
    setCardTarget(AnalysisProgressCard::expandedHeight);
}

void AnalysisView::onTrackStarted(const juce::String& path)
{
    const int idx = allIndexForPath(path);
    if (idx >= 0) {
        auto& e = m_allTracks[static_cast<size_t>(idx)];
        e.status = RowStatus::Running;
        e.progress = 50;
        m_progressCard->trackStarted(path, e.title);
        m_trackList->repaint();
    }
}

void AnalysisView::onTrackFinished(const Services::Analysis::TrackRowResult& result)
{
    const juce::String path(result.path);
    m_progressCard->trackFinished(path);

    const int idx = allIndexForPath(path);
    if (idx >= 0) {
        auto& e = m_allTracks[static_cast<size_t>(idx)];
        if (result.ok) {
            e.status = RowStatus::Done;
            e.progress = 100;
            e.analyzed = true;
            e.bpmValue = result.bpm;
            e.bpmConfidence = result.bpmConfidence;
            e.key = result.key;
            if (result.camelotKey.isNotEmpty())
                e.camelot = result.camelotKey.toUpperCase();
            e.energy = static_cast<float>(result.energy);
            e.lufs = result.lufs;
        } else {
            e.status = RowStatus::Error;
            e.progress = 0;
        }
        m_trackList->repaint();
    }

    if (result.ok) {
        m_progressCard->setLastResult(
            result.bpm > 0.0 ? juce::String(result.bpm, 1) : juce::String(),
            result.camelotKey.isNotEmpty() ? result.camelotKey : result.key,
            result.energy > 0 ? juce::String(result.energy) + "/10" : juce::String());
        if (m_detailPanel && m_detailPanel->hasTrack()
            && m_detailPanel->currentTrackId() == result.trackId)
            showDetailForEntry(idx);
    }
}

void AnalysisView::onBatchProgress(int processed, int total, int skipped)
{
    m_progressCard->setProgress(processed, total, skipped);
}

void AnalysisView::onBatchFinished(int processed, int total, int skipped, bool cancelled)
{
    m_progressCard->setProgress(processed, total, skipped);
    m_progressCard->endRun(cancelled);
    m_analyzing = false;
    m_analyzeAllBtn->setEnabled(true);
    m_analyzeSelectedBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->setButtonText(BM_TJ("common.cancel"));

    if (m_provider) {
        juce::Component::SafePointer<AnalysisView> self(this);
        juce::MessageManager::callAsync([self] {
            if (!self) return;
            self->loadAllFromDatabase();
            self->applyFilters();
        });
    }

    juce::Component::SafePointer<AnalysisView> self(this);
    juce::Timer::callAfterDelay(6000, [self] {
        if (self != nullptr && !self->m_analyzing)
            self->setCardTarget(0);
    });
    repaint();
}

void AnalysisView::setCardTarget(int target)
{
    if (m_cardTarget == target)
        return;
    m_cardTarget = target;
    startTimerHz(60);
}

void AnalysisView::timerCallback()
{
    const float target = static_cast<float>(m_cardTarget);
    const float diff = target - m_cardHeight;
    if (std::abs(diff) < 1.5f) {
        m_cardHeight = target;
        stopTimer();
    } else {
        m_cardHeight += diff * 0.22f;
    }
    resized();
    repaint();
}

void AnalysisView::paint(juce::Graphics& g)
{
    ProDraw::viewBackground(g, getWidth(), getHeight());

    const float fullW = static_cast<float>(getWidth());

    {
        float chipX = fullW - static_cast<float>(kMargin);
        auto chip = [&](const juce::String& text, juce::Colour colour) {
            const float cw = juce::GlyphArrangement::getStringWidth(Type::label(), text) + 30.0f;
            chipX -= cw;
            ProDraw::badge(g, text, chipX, static_cast<float>(kTitleY) + 5.0f, cw, 22.0f, colour);
            chipX -= 8.0f;
        };
        chip(juce::String(m_statTotal - m_statAnalyzed) + " " + BM_TJ("analysis.stats.pending"), Colors::warning());
        chip(juce::String(m_statAnalyzed) + " " + BM_TJ("analysis.stats.analyzed"), Colors::success());
        chip(juce::String(m_statTotal) + " " + BM_TJ("analysis.stats.tracks").toUpperCase(), Colors::primary());
    }

    ProDraw::glassPanel(g, { static_cast<float>(kMargin), static_cast<float>(kToolbarY),
                             fullW - kMargin * 2.0f, static_cast<float>(kToolbarH) }, 10.0f);
    ProDraw::glassPanel(g, { static_cast<float>(kMargin), static_cast<float>(kOptionsY),
                             fullW - kMargin * 2.0f, static_cast<float>(kOptionsH) }, 10.0f);

    if (m_trackList != nullptr) {
        auto listArea = m_trackList->getBounds().getUnion(m_listHeader->getBounds());
        ProDraw::glassPanel(g, listArea.toFloat().expanded(6.0f, 6.0f), 12.0f);

        if (m_trackModel && m_trackModel->visible.empty()) {
            ProDraw::emptyState(g, m_trackList->getBounds(),
                                juce::CharPointer_UTF8("\xe2\x99\xaa"),
                                BM_TJ("analysis.list.emptyTitle"),
                                BM_TJ("analysis.list.emptyBody"),
                                Colors::primary());
        }
    }

    ProDraw::vignette(g, fullW, static_cast<float>(getHeight()));
}

void AnalysisView::resized()
{
    const int w = getWidth();
    const int h = getHeight();
    static int lastLoggedW = -1;
    if (w != lastLoggedW) {
        lastLoggedW = w;
        spdlog::info("[AnalysisView] resized to {}x{}", w, h);
    }

    m_titleLabel->setBounds(kMargin, kTitleY, 300, 32);

    int fx = kMargin + 12;
    const int fy = kToolbarY + 8;
    m_searchLabel->setBounds(fx, fy, 66, 28);
    fx += 66;
    m_searchEditor->setBounds(fx, fy, 150, 28);
    fx += 158;
    m_genreLabel->setBounds(fx, fy, 44, 28);
    fx += 44;
    m_genreFilter->setBounds(fx, fy, 110, 28);
    fx += 118;
    m_statusFilterLabel->setBounds(fx, fy, 44, 28);
    fx += 44;
    m_statusFilter->setBounds(fx, fy, 130, 28);
    fx += 138;
    m_bpmRangeLabel->setBounds(fx, fy, 82, 28);
    fx += 84;
    int sliderW = juce::jmin(90, (w - fx - kMargin - 20) / 2);
    if (sliderW < 40) sliderW = 40;
    m_bpmMinSlider->setBounds(fx, fy, sliderW, 28);
    fx += sliderW + 4;
    m_bpmMaxSlider->setBounds(fx, fy, sliderW, 28);

    int ox = kMargin + 12;
    const int oy = kOptionsY + 7;
    const int optW = juce::jmin(110, (w - kMargin * 2 - 24 - 190) / 6);
    juce::ToggleButton* opts[] = { m_optBPM.get(), m_optKey.get(), m_optEnergy.get(),
                                   m_optStructure.get(), m_optWaveform.get(), m_optMood.get() };
    for (auto* o : opts) { o->setBounds(ox, oy, optW, 24); ox += optW; }
    m_optUltraStems->setBounds(ox, oy, juce::jmax(optW, 190), 24);

    const int cy = kActionsY;
    m_addTracksBtn->setBounds(kMargin, cy, 150, kActionsH);
    m_clearBtn->setBounds(kMargin + 158, cy, 100, kActionsH);
    m_selectAllBtn->setBounds(kMargin + 266, cy, 150, kActionsH);
    if (m_integrityBtn) m_integrityBtn->setBounds(kMargin + 424, cy, 150, kActionsH);
    m_trackCountLabel->setBounds(w - 500, cy, 100, kActionsH);
    m_analyzeSelectedBtn->setBounds(w - 392, cy, 150, kActionsH);
    m_analyzeAllBtn->setBounds(w - 234, cy, 130, kActionsH);
    m_cancelBtn->setBounds(w - 96, cy, 72, kActionsH);

    const int cardH = static_cast<int>(m_cardHeight + 0.5f);
    m_progressCard->setBounds(kMargin, kCardY, w - kMargin * 2, cardH);
    m_progressCard->setVisible(cardH > 4);

    const int contentTop = kCardY + cardH + (cardH > 4 ? 12 : 0);
    const int contentBottom = h - 14;

    const bool showDetail = w >= kDetailMinViewW;
    m_detailPanel->setVisible(showDetail);
    int listRight = w - kMargin;
    if (showDetail) {
        m_detailPanel->setBounds(w - kMargin - kDetailW, contentTop, kDetailW,
                                 contentBottom - contentTop);
        listRight = w - kMargin - kDetailW - 14;
    }

    const int listX = kMargin;
    const int listW = listRight - listX;
    m_listHeader->setBounds(listX, contentTop, listW, 28);
    m_trackList->setBounds(listX, contentTop + 32, listW,
                           juce::jmax(60, contentBottom - contentTop - 32));
}


namespace {

juce::Colour integrityStatusColour(Services::Analysis::AudioIntegrityChecker::Status s)
{
    using Status = Services::Analysis::AudioIntegrityChecker::Status;
    switch (s)
    {
        case Status::Ok:         return juce::Colour(0xff10b981);
        case Status::Warning:    return juce::Colour(0xfff59e0b);
        case Status::Corrupt:    return juce::Colour(0xffef4444);
        case Status::Unreadable: return juce::Colour(0xff9ca3af);
        case Status::Repaired:   return juce::Colour(0xff3b82f6);
        default:                 return juce::Colour(0xff6b7280);
    }
}

class IntegrityReportComponent : public juce::Component, public juce::ListBoxModel
{
public:
    using Status = Services::Analysis::AudioIntegrityChecker::Status;
    struct Row { juce::String path; Status status; juce::String details; };

    IntegrityReportComponent(std::vector<Row> r, int checkedTotal, std::function<void()> repairCb)
        : rows(std::move(r)), total(checkedTotal), onRepair(std::move(repairCb))
    {
        for (const auto& row : rows)
            if (row.status == Status::Corrupt || row.status == Status::Unreadable) ++corruptCount;

        list = std::make_unique<juce::ListBox>("integrityList", this);
        list->setRowHeight(46);
        list->setColour(juce::ListBox::backgroundColourId, Colors::bgDark());
        addAndMakeVisible(*list);

        repairBtn = std::make_unique<juce::TextButton>(
            juce::String::fromUTF8("R\xc3\xa9parer les fichiers corrompus (")
            + juce::String(corruptCount) + ")");
        repairBtn->setColour(juce::TextButton::buttonColourId, Colors::error().darker(0.2f));
        repairBtn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        repairBtn->setEnabled(corruptCount > 0);
        repairBtn->onClick = [this] {
            if (onRepair) onRepair();
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(1);
        };
        addAndMakeVisible(*repairBtn);

        openBtn = std::make_unique<juce::TextButton>(juce::String::fromUTF8("Ouvrir le dossier"));
        openBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
        openBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        openBtn->onClick = [this] {
            const int r2 = list->getSelectedRow();
            if (r2 >= 0 && r2 < (int) rows.size())
                juce::File(rows[(size_t) r2].path).revealToUser();
        };
        addAndMakeVisible(*openBtn);

        closeBtn = std::make_unique<juce::TextButton>(juce::String::fromUTF8("Fermer"));
        closeBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
        closeBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        closeBtn->onClick = [this] {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(0);
        };
        addAndMakeVisible(*closeBtn);

        setSize(880, 540);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(Colors::bgDark());
        g.setColour(Colors::textPrimary());
        g.setFont(juce::Font(16.0f, juce::Font::bold));
        g.drawText(juce::String::fromUTF8("Rapport d'int\xc3\xa9grit\xc3\xa9"), 20, 12, 500, 24,
                   juce::Justification::centredLeft);
        g.setColour(Colors::textSecondary());
        g.setFont(juce::Font(12.5f));
        juce::String sub;
        sub << total << juce::String::fromUTF8(" fichier(s) v\xc3\xa9rifi\xc3\xa9(s) \xc2\xb7 ")
            << (int) rows.size() << juce::String::fromUTF8(" probl\xc3\xa8me(s) \xc2\xb7 ")
            << corruptCount << juce::String::fromUTF8(" corrompu(s)");
        g.drawText(sub, 20, 36, 700, 18, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(16);
        area.removeFromTop(46);
        auto bottom = area.removeFromBottom(40);
        list->setBounds(area.reduced(0, 4));
        repairBtn->setBounds(bottom.removeFromLeft(280).reduced(0, 4));
        bottom.removeFromLeft(8);
        openBtn->setBounds(bottom.removeFromLeft(150).reduced(0, 4));
        closeBtn->setBounds(bottom.removeFromRight(110).reduced(0, 4));
    }

    int getNumRows() override { return (int) rows.size(); }

    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= (int) rows.size()) return;
        const auto& r2 = rows[(size_t) row];
        if (selected) { g.setColour(Colors::primary().withAlpha(0.15f)); g.fillRect(0, 0, w, h); }
        else if (row % 2 == 1) { g.setColour(juce::Colours::white.withAlpha(0.025f)); g.fillRect(0, 0, w, h); }

        const auto col = integrityStatusColour(r2.status);
        g.setColour(col.withAlpha(0.18f));
        g.fillRoundedRectangle(8.0f, 7.0f, 84.0f, 20.0f, 10.0f);
        g.setColour(col);
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(Services::Analysis::AudioIntegrityChecker::statusLabel(r2.status),
                   8, 7, 84, 20, juce::Justification::centred);

        g.setColour(Colors::textPrimary());
        g.setFont(juce::Font(12.5f));
        g.drawText(juce::File(r2.path).getFileName(), 102, 4, w - 112, 18, juce::Justification::centredLeft, true);
        g.setColour(Colors::textMuted());
        g.setFont(juce::Font(11.0f));
        juce::String detail = r2.details.upToFirstOccurrenceOf("\n", false, false);
        if (detail.isEmpty()) detail = juce::File(r2.path).getParentDirectory().getFullPathName();
        g.drawText(detail, 102, 24, w - 112, 16, juce::Justification::centredLeft, true);
    }

private:
    std::vector<Row> rows;
    int total = 0;
    int corruptCount = 0;
    std::function<void()> onRepair;
    std::unique_ptr<juce::ListBox> list;
    std::unique_ptr<juce::TextButton> repairBtn, openBtn, closeBtn;
};

}

void AnalysisView::showIntegrityMenu()
{
    if (m_integrityRunning.load())
    {
        m_integrityCancel.store(true);
        return;
    }

    if (! Services::Analysis::AudioIntegrityChecker::isAvailable())
    {
        Widgets::ToastNotifier::getInstance().show(
            juce::String::fromUTF8("Int\xc3\xa9grit\xc3\xa9"),
            juce::String::fromUTF8("ffmpeg est introuvable \xe2\x80\x94 r\xc3\xa9installez BeatMate."),
            Widgets::ToastNotifier::Kind::Error, 6000);
        return;
    }

    const auto selected = getSelectedPaths();
    juce::StringArray visiblePaths;
    if (m_trackModel)
        for (int idx : m_trackModel->visible)
            if (idx >= 0 && idx < (int) m_allTracks.size())
                visiblePaths.add(m_allTracks[(size_t) idx].path);
    juce::StringArray allPaths;
    for (const auto& t : m_allTracks) allPaths.add(t.path);

    int knownIssues = (int) m_integrityIssues.size();

    juce::PopupMenu m;
    m.addSectionHeader(juce::String::fromUTF8("Contr\xc3\xb4le d'int\xc3\xa9grit\xc3\xa9 des fichiers"));
    m.addItem(1, juce::String::fromUTF8("V\xc3\xa9rifier la s\xc3\xa9lection (")
                 + juce::String(selected.size()) + ")", selected.size() > 0);
    m.addItem(2, juce::String::fromUTF8("V\xc3\xa9rifier les morceaux affich\xc3\xa9s (")
                 + juce::String(visiblePaths.size()) + ")", visiblePaths.size() > 0);
    m.addItem(3, juce::String::fromUTF8("V\xc3\xa9rifier toute la biblioth\xc3\xa8que (")
                 + juce::String(allPaths.size()) + ")", allPaths.size() > 0);
    m.addSeparator();
    m.addItem(4, juce::String::fromUTF8("Rapport d'int\xc3\xa9grit\xc3\xa9\xe2\x80\xa6 (")
                 + juce::String(knownIssues) + juce::String::fromUTF8(" probl\xc3\xa8me(s))"), knownIssues > 0);

    juce::Component::SafePointer<AnalysisView> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(m_integrityBtn.get()),
        [safe, selected, visiblePaths, allPaths](int r)
        {
            if (safe == nullptr) return;
            switch (r)
            {
                case 1: safe->startIntegrityScan(selected); break;
                case 2: safe->startIntegrityScan(visiblePaths); break;
                case 3: safe->startIntegrityScan(allPaths); break;
                case 4: safe->showIntegrityReportDialog(); break;
                default: break;
            }
        });
}

void AnalysisView::joinIntegrityWorkers()
{
    for (auto& w : m_integrityWorkers)
        if (w.joinable()) w.join();
    m_integrityWorkers.clear();
}

void AnalysisView::updateIntegrityButton()
{
    if (! m_integrityBtn) return;
    if (m_integrityRunning.load())
        m_integrityBtn->setButtonText(juce::String(m_integrityDone.load()) + "/"
                                      + juce::String(m_integrityPaths.size())
                                      + juce::String::fromUTF8("  \xc2\xb7  Stop"));
    else
        m_integrityBtn->setButtonText(juce::String::fromUTF8("V\xc3\xa9rifier / R\xc3\xa9parer \xe2\x96\xbe"));
}

void AnalysisView::startIntegrityScan(juce::StringArray paths)
{
    paths.removeEmptyStrings();
    paths.removeDuplicates(true);
    if (paths.isEmpty() || m_integrityRunning.load()) return;

    joinIntegrityWorkers();
    m_integrityPaths = paths;
    m_integrityIssues.clear();
    m_integrityRunning.store(true);
    m_integrityCancel.store(false);
    m_integrityNext.store(0);
    m_integrityDone.store(0);
    updateIntegrityButton();

    const int toastId = Widgets::ToastNotifier::getInstance().show(
        juce::String::fromUTF8("V\xc3\xa9rification d'int\xc3\xa9grit\xc3\xa9"),
        juce::String("0/") + juce::String(paths.size()),
        Widgets::ToastNotifier::Kind::Progress, 0);
    juce::Component::SafePointer<AnalysisView> safe(this);
    Widgets::ToastNotifier::getInstance().setCancelCallback(toastId, [safe] {
        if (safe != nullptr) safe->m_integrityCancel.store(true);
    });

    const int nWorkers = juce::jlimit(1, 4, (int) std::thread::hardware_concurrency() / 2);
    for (int wi = 0; wi < nWorkers; ++wi)
    {
        m_integrityWorkers.emplace_back([safe, toastId]
        {
            while (true)
            {
                if (safe == nullptr) return;
                if (safe->m_integrityCancel.load()) break;
                const int idx = safe->m_integrityNext.fetch_add(1);
                if (idx >= safe->m_integrityPaths.size()) break;
                const juce::String path = safe->m_integrityPaths[idx];

                auto report = safe->m_integrity->check(path);
                const int done = safe->m_integrityDone.fetch_add(1) + 1;

                juce::MessageManager::callAsync([safe, toastId, path, report, done]
                {
                    if (safe == nullptr) return;
                    using Status = Services::Analysis::AudioIntegrityChecker::Status;
                    if (report.status != Status::Ok && report.status != Status::Repaired)
                        safe->m_integrityIssues.push_back({ path, report.status, report.details });
                    Widgets::ToastNotifier::getInstance().update(toastId,
                        juce::String(done) + "/" + juce::String(safe->m_integrityPaths.size())
                        + juce::String::fromUTF8("  \xc2\xb7  ") + juce::File(path).getFileName());
                    safe->updateIntegrityButton();

                    const bool finished = done >= safe->m_integrityPaths.size()
                                          || safe->m_integrityCancel.load();
                    if (finished && safe->m_integrityRunning.exchange(false))
                    {
                        Widgets::ToastNotifier::getInstance().dismiss(toastId);
                        safe->updateIntegrityButton();
                        const int issues = (int) safe->m_integrityIssues.size();
                        if (issues > 0)
                        {
                            Widgets::ToastNotifier::getInstance().show(
                                juce::String::fromUTF8("Int\xc3\xa9grit\xc3\xa9 termin\xc3\xa9""e"),
                                juce::String(issues) + juce::String::fromUTF8(" probl\xc3\xa8me(s) d\xc3\xa9tect\xc3\xa9(s)"),
                                Widgets::ToastNotifier::Kind::Warning, 6000);
                            safe->showIntegrityReportDialog();
                        }
                        else
                        {
                            Widgets::ToastNotifier::getInstance().show(
                                juce::String::fromUTF8("Int\xc3\xa9grit\xc3\xa9 termin\xc3\xa9""e"),
                                juce::String::fromUTF8("Tous les fichiers v\xc3\xa9rifi\xc3\xa9s sont intacts."),
                                Widgets::ToastNotifier::Kind::Success, 6000);
                        }
                    }
                });
            }
        });
    }
}

void AnalysisView::showIntegrityReportDialog()
{
    std::vector<IntegrityReportComponent::Row> rows;
    for (const auto& issue : m_integrityIssues)
        rows.push_back({ issue.path, issue.status, issue.details });

    juce::Component::SafePointer<AnalysisView> safe(this);
    auto* content = new IntegrityReportComponent(std::move(rows), m_integrityDone.load(),
        [safe] { if (safe != nullptr) safe->repairCorruptFiles(); });

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(content);
    opts.dialogTitle = juce::String::fromUTF8("Rapport d'int\xc3\xa9grit\xc3\xa9 \xe2\x80\x94 BeatMate");
    opts.dialogBackgroundColour = Colors::bgDark();
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = true;
    opts.launchAsync();
}

void AnalysisView::repairCorruptFiles()
{
    using Status = Services::Analysis::AudioIntegrityChecker::Status;
    juce::StringArray toRepair;
    for (const auto& issue : m_integrityIssues)
        if (issue.status == Status::Corrupt || issue.status == Status::Unreadable)
            toRepair.add(issue.path);
    toRepair.removeDuplicates(true);
    if (toRepair.isEmpty() || m_integrityRunning.load()) return;

    joinIntegrityWorkers();
    m_integrityRunning.store(true);
    m_integrityCancel.store(false);

    const int toastId = Widgets::ToastNotifier::getInstance().show(
        juce::String::fromUTF8("R\xc3\xa9paration en cours"),
        juce::String("0/") + juce::String(toRepair.size()),
        Widgets::ToastNotifier::Kind::Progress, 0);

    juce::Component::SafePointer<AnalysisView> safe(this);
    m_integrityWorkers.emplace_back([safe, toRepair, toastId]
    {
        int repaired = 0, failed = 0;
        for (int i = 0; i < toRepair.size(); ++i)
        {
            if (safe == nullptr) return;
            if (safe->m_integrityCancel.load()) break;
            juce::String err;
            const auto report = safe->m_integrity->repair(toRepair[i], &err);
            const bool ok = report.status == Status::Repaired || report.status == Status::Ok;
            ok ? ++repaired : ++failed;
            const juce::String path = toRepair[i];
            const int done = i + 1;
            juce::MessageManager::callAsync([safe, toastId, path, done, total = toRepair.size(), ok, report]
            {
                if (safe == nullptr) return;
                Widgets::ToastNotifier::getInstance().update(toastId,
                    juce::String(done) + "/" + juce::String(total)
                    + juce::String::fromUTF8("  \xc2\xb7  ") + juce::File(path).getFileName());
                for (auto& issue : safe->m_integrityIssues)
                    if (issue.path == path) { issue.status = report.status; issue.details = report.details; }
            });
        }
        juce::MessageManager::callAsync([safe, toastId, repaired, failed]
        {
            if (safe == nullptr) return;
            safe->m_integrityRunning.store(false);
            safe->updateIntegrityButton();
            Widgets::ToastNotifier::getInstance().dismiss(toastId);
            juce::String msg;
            msg << repaired << juce::String::fromUTF8(" fichier(s) r\xc3\xa9par\xc3\xa9(s)");
            if (failed > 0) msg << juce::String::fromUTF8(", ") << failed
                                << juce::String::fromUTF8(" \xc3\xa9""chec(s)");
            msg << juce::String::fromUTF8(". Originaux sauvegard\xc3\xa9s dans integrity_backups.");
            Widgets::ToastNotifier::getInstance().show(
                juce::String::fromUTF8("R\xc3\xa9paration termin\xc3\xa9""e"), msg,
                failed > 0 ? Widgets::ToastNotifier::Kind::Warning : Widgets::ToastNotifier::Kind::Success, 8000);
            safe->showIntegrityReportDialog();
        });
    });
}

}
