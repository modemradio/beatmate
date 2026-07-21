#include "LibraryBrowserPanel.h"
#include "../../../services/library/TrackDataProvider.h"
#include "../../../services/config/I18n.h"
#include <algorithm>
#include <cmath>

namespace BeatMate::UI {

namespace Colors {
    static const juce::Colour background     { 0xFF1A1A2E };
    static const juce::Colour panelBg        { 0xFF16213E };
    static const juce::Colour surface        { 0xFF0F3460 };
    static const juce::Colour primary        { 0xFF3B82F6 };
    static const juce::Colour primaryDim     { 0xFF2563EB };
    static const juce::Colour accent         { 0xFF8B5CF6 };
    static const juce::Colour textBright     { 0xFFE2E8F0 };
    static const juce::Colour textSecondary  { 0xFF94A3B8 };
    static const juce::Colour textDim        { 0xFF64748B };
    static const juce::Colour rowEven        { 0xFF1E293B };
    static const juce::Colour rowOdd         { 0xFF1A2332 };
    static const juce::Colour rowSelected    { 0xFF1E3A5F };
    static const juce::Colour separator      { 0xFF334155 };
    static const juce::Colour energyLow      { 0xFF22C55E };
    static const juce::Colour energyMid      { 0xFFF59E0B };
    static const juce::Colour energyHigh     { 0xFFEF4444 };
    static const juce::Colour warning        { 0xFFF59E0B };
    static const juce::Colour badge          { 0xFF3B82F6 };
}

static juce::String formatDuration(double seconds)
{
    int mins = static_cast<int>(seconds) / 60;
    int secs = static_cast<int>(seconds) % 60;
    return juce::String::formatted("%d:%02d", mins, secs);
}

static juce::Colour getEnergyColour(float energy)
{
    float norm = juce::jlimit(0.0f, 1.0f, (energy - 1.0f) / 9.0f);
    if (norm < 0.5f)
        return Colors::energyLow.interpolatedWith(Colors::energyMid, norm * 2.0f);
    return Colors::energyMid.interpolatedWith(Colors::energyHigh, (norm - 0.5f) * 2.0f);
}

LibraryBrowserPanel::LibraryBrowserPanel(Services::Library::TrackDataProvider* provider)
    : m_provider(provider)
{
    setupUI();

    if (m_provider)
    {
        m_provider->onDataChanged([this]() {
            juce::MessageManager::callAsync([this]() { refreshResults(); });
        });
        populateGenreCombo();
        populateKeyCombo();
        m_resultCountLabel->setText(BM_TJ("library.search"), juce::dontSendNotification);
    }
}

void LibraryBrowserPanel::setupUI()
{
    m_titleLabel = std::make_unique<juce::Label>("title", BM_TJ("library.title"));
    m_titleLabel->setFont(juce::Font(14.0f, juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId, Colors::primary);
    addAndMakeVisible(m_titleLabel.get());

    m_resultCountLabel = std::make_unique<juce::Label>("count", "0 tracks");
    m_resultCountLabel->setFont(juce::Font(11.0f));
    m_resultCountLabel->setColour(juce::Label::textColourId, Colors::textDim);
    m_resultCountLabel->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(m_resultCountLabel.get());

    m_searchEdit = std::make_unique<juce::TextEditor>("search");
    m_searchEdit->setTextToShowWhenEmpty(BM_TJ("widget.LibraryBrowserPanel.searchPlaceholder"), Colors::textDim);
    m_searchEdit->setColour(juce::TextEditor::backgroundColourId, Colors::surface);
    m_searchEdit->setColour(juce::TextEditor::textColourId, Colors::textBright);
    m_searchEdit->setColour(juce::TextEditor::outlineColourId, Colors::separator);
    m_searchEdit->setColour(juce::TextEditor::focusedOutlineColourId, Colors::primary);
    m_searchEdit->onTextChange = [this]() {
        stopTimer();
        startTimer(300);
    };
    addAndMakeVisible(m_searchEdit.get());

    m_bpmLabel = std::make_unique<juce::Label>("bpmLbl", "BPM");
    m_bpmLabel->setFont(juce::Font(10.0f, juce::Font::bold));
    m_bpmLabel->setColour(juce::Label::textColourId, Colors::textSecondary);
    addAndMakeVisible(m_bpmLabel.get());

    m_bpmMin = std::make_unique<juce::Slider>(juce::Slider::LinearBar, juce::Slider::NoTextBox);
    m_bpmMin->setRange(60, 200, 1);
    m_bpmMin->setValue(60);
    m_bpmMin->setColour(juce::Slider::trackColourId, Colors::primaryDim);
    m_bpmMin->setColour(juce::Slider::backgroundColourId, Colors::surface);
    m_bpmMin->onValueChange = [this]() { performSearch(); };
    addAndMakeVisible(m_bpmMin.get());

    m_bpmMax = std::make_unique<juce::Slider>(juce::Slider::LinearBar, juce::Slider::NoTextBox);
    m_bpmMax->setRange(60, 200, 1);
    m_bpmMax->setValue(200);
    m_bpmMax->setColour(juce::Slider::trackColourId, Colors::primaryDim);
    m_bpmMax->setColour(juce::Slider::backgroundColourId, Colors::surface);
    m_bpmMax->onValueChange = [this]() { performSearch(); };
    addAndMakeVisible(m_bpmMax.get());

    m_keyCombo = std::make_unique<juce::ComboBox>("key");
    m_keyCombo->setColour(juce::ComboBox::backgroundColourId, Colors::surface);
    m_keyCombo->setColour(juce::ComboBox::textColourId, Colors::textBright);
    m_keyCombo->setColour(juce::ComboBox::outlineColourId, Colors::separator);
    m_keyCombo->onChange = [this]() { performSearch(); };
    addAndMakeVisible(m_keyCombo.get());

    m_genreCombo = std::make_unique<juce::ComboBox>("genre");
    m_genreCombo->setColour(juce::ComboBox::backgroundColourId, Colors::surface);
    m_genreCombo->setColour(juce::ComboBox::textColourId, Colors::textBright);
    m_genreCombo->setColour(juce::ComboBox::outlineColourId, Colors::separator);
    m_genreCombo->onChange = [this]() { performSearch(); };
    addAndMakeVisible(m_genreCombo.get());

    m_energyLabel = std::make_unique<juce::Label>("nrgLbl", BM_TJ("analysis.opt.energy"));
    m_energyLabel->setFont(juce::Font(10.0f, juce::Font::bold));
    m_energyLabel->setColour(juce::Label::textColourId, Colors::textSecondary);
    addAndMakeVisible(m_energyLabel.get());

    m_energyMin = std::make_unique<juce::Slider>(juce::Slider::LinearBar, juce::Slider::NoTextBox);
    m_energyMin->setRange(1, 10, 1);
    m_energyMin->setValue(1);
    m_energyMin->setColour(juce::Slider::trackColourId, Colors::energyLow);
    m_energyMin->setColour(juce::Slider::backgroundColourId, Colors::surface);
    m_energyMin->onValueChange = [this]() { performSearch(); };
    addAndMakeVisible(m_energyMin.get());

    m_energyMax = std::make_unique<juce::Slider>(juce::Slider::LinearBar, juce::Slider::NoTextBox);
    m_energyMax->setRange(1, 10, 1);
    m_energyMax->setValue(10);
    m_energyMax->setColour(juce::Slider::trackColourId, Colors::energyHigh);
    m_energyMax->setColour(juce::Slider::backgroundColourId, Colors::surface);
    m_energyMax->onValueChange = [this]() { performSearch(); };
    addAndMakeVisible(m_energyMax.get());

    m_ratingCombo = std::make_unique<juce::ComboBox>("rating");
    m_ratingCombo->addItem(BM_TJ("widget.LibraryBrowserPanel.rating.all"), 1);
    m_ratingCombo->addItem(juce::String(juce::CharPointer_UTF8("\xe2\x98\x85")) + " 1+", 2);
    m_ratingCombo->addItem(juce::String(juce::CharPointer_UTF8("\xe2\x98\x85")) + " 2+", 3);
    m_ratingCombo->addItem(juce::String(juce::CharPointer_UTF8("\xe2\x98\x85")) + " 3+", 4);
    m_ratingCombo->addItem(juce::String(juce::CharPointer_UTF8("\xe2\x98\x85")) + " 4+", 5);
    m_ratingCombo->addItem(juce::String(juce::CharPointer_UTF8("\xe2\x98\x85")) + " 5", 6);
    m_ratingCombo->setSelectedId(1, juce::dontSendNotification);
    m_ratingCombo->setColour(juce::ComboBox::backgroundColourId, Colors::surface);
    m_ratingCombo->setColour(juce::ComboBox::textColourId, Colors::textBright);
    m_ratingCombo->setColour(juce::ComboBox::outlineColourId, Colors::separator);
    m_ratingCombo->onChange = [this]() { performSearch(); };
    addAndMakeVisible(m_ratingCombo.get());

    m_resetBtn = std::make_unique<juce::TextButton>(BM_TJ("settings.reset"));
    m_resetBtn->setColour(juce::TextButton::buttonColourId, Colors::surface);
    m_resetBtn->setColour(juce::TextButton::textColourOffId, Colors::warning);
    m_resetBtn->onClick = [this]() { clearFilters(); };
    addAndMakeVisible(m_resetBtn.get());

    m_tableModel = std::make_unique<TrackTableModel>();
    m_tableModel->tracks = &m_results;
    m_tableModel->owner = this;

    m_table = std::make_unique<DragSourceTable>("LibraryResults", m_tableModel.get());
    m_table->setColour(juce::ListBox::backgroundColourId, Colors::panelBg);
    m_table->setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    m_table->setRowHeight(36);
    m_table->setMultipleSelectionEnabled(true);
    m_table->getHeader().setStretchToFitActive(true);

    m_table->getHeader().addColumn(BM_TJ("widget.LibraryBrowserPanel.col.title"),    Col_Title,   140, 80, 400, juce::TableHeaderComponent::defaultFlags);
    m_table->getHeader().addColumn(BM_TJ("widget.LibraryBrowserPanel.col.artist"),  Col_Artist,  110, 60, 300, juce::TableHeaderComponent::defaultFlags);
    m_table->getHeader().addColumn(BM_TJ("widget.LibraryBrowserPanel.col.bpm"),      Col_BPM,      52, 40,  70, juce::TableHeaderComponent::defaultFlags);
    m_table->getHeader().addColumn(BM_TJ("widget.LibraryBrowserPanel.col.key"),      Col_Key,      44, 35,  60, juce::TableHeaderComponent::defaultFlags);
    m_table->getHeader().addColumn(BM_TJ("widget.LibraryBrowserPanel.col.energy"),  Col_Energy,   52, 40,  70, juce::TableHeaderComponent::defaultFlags);
    m_table->getHeader().addColumn(BM_TJ("widget.LibraryBrowserPanel.col.duration"),    Col_Duration,  52, 40,  70, juce::TableHeaderComponent::defaultFlags);
    m_table->getHeader().addColumn(BM_TJ("widget.LibraryBrowserPanel.col.genre"),    Col_Genre,    80, 50, 200, juce::TableHeaderComponent::defaultFlags);
    m_table->getHeader().addColumn(BM_TJ("widget.LibraryBrowserPanel.col.rating"),     Col_Rating,   44, 35,  60, juce::TableHeaderComponent::defaultFlags);

    addAndMakeVisible(m_table.get());
}

void LibraryBrowserPanel::populateGenreCombo()
{
    if (!m_provider) return;
    m_genreCombo->clear(juce::dontSendNotification);
    m_genreCombo->addItem(BM_TJ("widget.LibraryBrowserPanel.genre.all"), 1);
    auto genres = m_provider->getGenreDistribution();
    int id = 2;
    for (auto& [genre, count] : genres)
    {
        if (!genre.empty())
            m_genreCombo->addItem(juce::String(genre) + " (" + juce::String(count) + ")", id++);
    }
    m_genreCombo->setSelectedId(1, juce::dontSendNotification);
}

void LibraryBrowserPanel::populateKeyCombo()
{
    m_keyCombo->clear(juce::dontSendNotification);
    m_keyCombo->addItem(BM_TJ("widget.LibraryBrowserPanel.key.all"), 1);
    int id = 2;
    for (int n = 1; n <= 12; ++n)
    {
        m_keyCombo->addItem(juce::String(n) + "A", id++);
        m_keyCombo->addItem(juce::String(n) + "B", id++);
    }
    m_keyCombo->setSelectedId(1, juce::dontSendNotification);
}

void LibraryBrowserPanel::timerCallback()
{
    stopTimer();
    performSearch();
}

void LibraryBrowserPanel::performSearch()
{
    if (!m_provider) return;

    auto searchText = m_searchEdit->getText().toStdString();
    float bpmMinVal = static_cast<float>(m_bpmMin->getValue());
    float bpmMaxVal = static_cast<float>(m_bpmMax->getValue());
    float eMin = static_cast<float>(m_energyMin->getValue());
    float eMax = static_cast<float>(m_energyMax->getValue());

    std::string keyFilter;
    if (m_keyCombo->getSelectedId() > 1)
        keyFilter = m_keyCombo->getText().toStdString();

    std::string genreFilter;
    if (m_genreCombo->getSelectedId() > 1)
    {
        auto genreText = m_genreCombo->getText();
        int parenIdx = genreText.lastIndexOf(" (");
        genreFilter = (parenIdx > 0 ? genreText.substring(0, parenIdx) : genreText).toStdString();
    }

    int ratingMin = 0;
    if (m_ratingCombo->getSelectedId() > 1)
        ratingMin = m_ratingCombo->getSelectedId() - 1;

    bool hasFilter = !searchText.empty() || bpmMinVal > 60 || bpmMaxVal < 200 ||
                     !keyFilter.empty() || !genreFilter.empty() ||
                     eMin > 1 || eMax < 10 || ratingMin > 0;

    if (!hasFilter)
    {
        m_results = m_provider->getRecentlyAdded(200);
    }
    else
    {
        m_results = m_provider->getTracksByFilterWithSearch(
            searchText, bpmMinVal, bpmMaxVal, keyFilter, genreFilter,
            /*artist=*/"", eMin, eMax, ratingMin);

        if (m_results.size() > 500)
            m_results.resize(500);
    }

    auto& res = m_results;
    bool fwd = m_sortForward;
    switch (m_sortColumnId)
    {
        case Col_Title:
            std::sort(res.begin(), res.end(), [fwd](auto& a, auto& b) {
                return fwd ? a.title < b.title : a.title > b.title;
            });
            break;
        case Col_Artist:
            std::sort(res.begin(), res.end(), [fwd](auto& a, auto& b) {
                return fwd ? a.artist < b.artist : a.artist > b.artist;
            });
            break;
        case Col_BPM:
            std::sort(res.begin(), res.end(), [fwd](auto& a, auto& b) {
                return fwd ? a.bpm < b.bpm : a.bpm > b.bpm;
            });
            break;
        case Col_Key:
            std::sort(res.begin(), res.end(), [fwd](auto& a, auto& b) {
                return fwd ? a.camelotKey < b.camelotKey : a.camelotKey > b.camelotKey;
            });
            break;
        case Col_Energy:
            std::sort(res.begin(), res.end(), [fwd](auto& a, auto& b) {
                return fwd ? a.energy < b.energy : a.energy > b.energy;
            });
            break;
        case Col_Duration:
            std::sort(res.begin(), res.end(), [fwd](auto& a, auto& b) {
                return fwd ? a.duration < b.duration : a.duration > b.duration;
            });
            break;
        case Col_Genre:
            std::sort(res.begin(), res.end(), [fwd](auto& a, auto& b) {
                return fwd ? a.genre < b.genre : a.genre > b.genre;
            });
            break;
        case Col_Rating:
            std::sort(res.begin(), res.end(), [fwd](auto& a, auto& b) {
                return fwd ? a.rating < b.rating : a.rating > b.rating;
            });
            break;
        default:
            break;
    }

    m_table->updateContent();
    m_table->repaint();
    m_resultCountLabel->setText(juce::String(m_results.size()) + " tracks", juce::dontSendNotification);
    spdlog::info("[LibraryBrowserPanel] performSearch -> {} tracks (hasFilter={}, provider={})",
                 m_results.size(), hasFilter, (void*)m_provider);
}

void LibraryBrowserPanel::refreshResults()
{
    spdlog::info("[LibraryBrowserPanel] refreshResults called, provider={}", (void*)m_provider);
    performSearch();
}

void LibraryBrowserPanel::setProvider(Services::Library::TrackDataProvider* p)
{
    if (p == m_provider) return;
    m_provider = p;
    if (m_provider) {
        m_provider->onDataChanged([this]() {
            juce::MessageManager::callAsync([this]() { refreshResults(); });
        });
        populateGenreCombo();
        populateKeyCombo();
        refreshResults();
    }
}

void LibraryBrowserPanel::clearFilters()
{
    m_searchEdit->clear();
    m_bpmMin->setValue(60, juce::dontSendNotification);
    m_bpmMax->setValue(200, juce::dontSendNotification);
    m_keyCombo->setSelectedId(1, juce::dontSendNotification);
    m_genreCombo->setSelectedId(1, juce::dontSendNotification);
    m_energyMin->setValue(1, juce::dontSendNotification);
    m_energyMax->setValue(10, juce::dontSendNotification);
    m_ratingCombo->setSelectedId(1, juce::dontSendNotification);
    performSearch();
}

std::vector<Models::Track> LibraryBrowserPanel::getSelectedTracks() const
{
    std::vector<Models::Track> selected;
    auto sel = m_table->getSelectedRows();
    for (int i = 0; i < sel.size(); ++i)
    {
        int idx = sel[i];
        if (idx >= 0 && idx < static_cast<int>(m_results.size()))
            selected.push_back(m_results[static_cast<size_t>(idx)]);
    }
    return selected;
}

void LibraryBrowserPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setGradientFill(juce::ColourGradient(
        Colors::panelBg, bounds.getX(), bounds.getY(),
        Colors::background, bounds.getX(), bounds.getBottom(), false));
    g.fillRoundedRectangle(bounds, 6.0f);

    g.setColour(Colors::separator.withAlpha(0.5f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

    int filterBottom = 170;
    g.setColour(Colors::separator.withAlpha(0.3f));
    g.drawHorizontalLine(filterBottom, 8.0f, static_cast<float>(getWidth()) - 8.0f);
}

void LibraryBrowserPanel::resized()
{
    auto area = getLocalBounds().reduced(8);
    int spacing = 4;

    auto titleRow = area.removeFromTop(22);
    m_titleLabel->setBounds(titleRow.removeFromLeft(titleRow.getWidth() / 2));
    m_resultCountLabel->setBounds(titleRow);
    area.removeFromTop(spacing);

    m_searchEdit->setBounds(area.removeFromTop(28));
    area.removeFromTop(spacing);

    auto bpmRow = area.removeFromTop(22);
    m_bpmLabel->setBounds(bpmRow.removeFromLeft(32));
    auto bpmSliderArea = bpmRow;
    m_bpmMin->setBounds(bpmSliderArea.removeFromLeft(bpmSliderArea.getWidth() / 2 - 2));
    bpmSliderArea.removeFromLeft(4);
    m_bpmMax->setBounds(bpmSliderArea);
    area.removeFromTop(spacing);

    auto filterRow1 = area.removeFromTop(24);
    m_keyCombo->setBounds(filterRow1.removeFromLeft(filterRow1.getWidth() / 2 - 2));
    filterRow1.removeFromLeft(4);
    m_genreCombo->setBounds(filterRow1);
    area.removeFromTop(spacing);

    auto energyRow = area.removeFromTop(22);
    m_energyLabel->setBounds(energyRow.removeFromLeft(42));
    auto eSliderArea = energyRow;
    m_energyMin->setBounds(eSliderArea.removeFromLeft(eSliderArea.getWidth() / 2 - 2));
    eSliderArea.removeFromLeft(4);
    m_energyMax->setBounds(eSliderArea);
    area.removeFromTop(spacing);

    auto ratingRow = area.removeFromTop(24);
    m_ratingCombo->setBounds(ratingRow.removeFromLeft(ratingRow.getWidth() / 2 - 2));
    ratingRow.removeFromLeft(4);
    m_resetBtn->setBounds(ratingRow);
    area.removeFromTop(spacing + 4);

    m_table->setBounds(area);
}

void LibraryBrowserPanel::TrackTableModel::paintRowBackground(juce::Graphics& g, int row, int w, int h, bool selected)
{
    if (selected)
        g.fillAll(Colors::rowSelected);
    else
        g.fillAll(row % 2 == 0 ? Colors::rowEven : Colors::rowOdd);

    g.setColour(Colors::separator.withAlpha(0.15f));
    g.drawHorizontalLine(h - 1, 0.0f, static_cast<float>(w));
}

void LibraryBrowserPanel::TrackTableModel::paintCell(juce::Graphics& g, int row, int columnId,
                                                       int w, int h, bool selected)
{
    if (!tracks || row < 0 || row >= static_cast<int>(tracks->size()))
        return;

    auto& track = (*tracks)[static_cast<size_t>(row)];
    g.setFont(juce::Font(12.0f));

    auto cellBounds = juce::Rectangle<int>(0, 0, w, h).reduced(4, 2);

    switch (columnId)
    {
        case Col_Title:
        {
            g.setFont(juce::Font(12.0f, juce::Font::bold));
            g.setColour(Colors::textBright);
            g.drawText(juce::String(track.title), cellBounds, juce::Justification::centredLeft, true);
            break;
        }
        case Col_Artist:
        {
            g.setColour(Colors::textSecondary);
            g.drawText(juce::String(track.artist), cellBounds, juce::Justification::centredLeft, true);
            break;
        }
        case Col_BPM:
        {
            auto pillBounds = cellBounds.toFloat().reduced(1, 4);
            g.setColour(Colors::badge.withAlpha(0.2f));
            g.fillRoundedRectangle(pillBounds, 8.0f);
            g.setColour(Colors::badge);
            g.setFont(juce::Font(11.0f, juce::Font::bold));
            g.drawText(juce::String(track.bpm, 1), cellBounds, juce::Justification::centred, false);
            break;
        }
        case Col_Key:
        {
            auto pillBounds = cellBounds.toFloat().reduced(1, 4);
            g.setColour(Colors::accent.withAlpha(0.2f));
            g.fillRoundedRectangle(pillBounds, 8.0f);
            g.setColour(Colors::accent);
            g.setFont(juce::Font(11.0f, juce::Font::bold));
            auto keyStr = track.camelotKey.empty() ? juce::String(track.key) : juce::String(track.camelotKey);
            g.drawText(keyStr, cellBounds, juce::Justification::centred, false);
            break;
        }
        case Col_Energy:
        {
            float norm = juce::jlimit(0.0f, 1.0f, (track.energy - 1.0f) / 9.0f);
            auto barBounds = cellBounds.toFloat().reduced(2, 10);
            g.setColour(Colors::surface);
            g.fillRoundedRectangle(barBounds, 3.0f);
            auto fillBounds = barBounds.withWidth(barBounds.getWidth() * norm);
            g.setColour(getEnergyColour(track.energy));
            g.fillRoundedRectangle(fillBounds, 3.0f);
            g.setColour(Colors::textBright);
            g.setFont(juce::Font(10.0f, juce::Font::bold));
            g.drawText(juce::String(static_cast<int>(track.energy)), cellBounds, juce::Justification::centred, false);
            break;
        }
        case Col_Duration:
        {
            g.setColour(Colors::textDim);
            g.setFont(juce::Font(11.0f));
            g.drawText(formatDuration(track.duration), cellBounds, juce::Justification::centred, false);
            break;
        }
        case Col_Genre:
        {
            g.setColour(Colors::textSecondary);
            g.setFont(juce::Font(11.0f));
            g.drawText(juce::String(track.genre), cellBounds, juce::Justification::centredLeft, true);
            break;
        }
        case Col_Rating:
        {
            g.setColour(Colors::warning);
            g.setFont(juce::Font(11.0f));
            juce::String stars;
            for (int i = 0; i < track.rating; ++i)
                stars += juce::String(juce::CharPointer_UTF8("\xe2\x98\x85"));
            g.drawText(stars, cellBounds, juce::Justification::centred, false);
            break;
        }
        default:
            break;
    }
}

void LibraryBrowserPanel::TrackTableModel::cellDoubleClicked(int row, int /*columnId*/, const juce::MouseEvent&)
{
    if (!tracks || !owner || row < 0 || row >= static_cast<int>(tracks->size()))
        return;
    auto& track = (*tracks)[static_cast<size_t>(row)];
    owner->m_listeners.call([&track](Listener& l) { l.trackDoubleClicked(track); });
    owner->m_listeners.call([&track](Listener& l) { l.addTrackRequested(track); });
}

void LibraryBrowserPanel::TrackTableModel::sortOrderChanged(int newSortColumnId, bool isForwards)
{
    if (owner)
    {
        owner->m_sortColumnId = newSortColumnId;
        owner->m_sortForward = isForwards;
        owner->performSearch();
    }
}

juce::var LibraryBrowserPanel::TrackTableModel::getDragSourceDescription(const juce::SparseSet<int>& selectedRows)
{
    if (!tracks) return {};

    juce::String ids = "BEATMATE_TRACKS:";
    for (int i = 0; i < selectedRows.size(); ++i)
    {
        int idx = selectedRows[i];
        if (idx >= 0 && idx < static_cast<int>(tracks->size()))
        {
            if (i > 0) ids += ",";
            ids += juce::String((*tracks)[static_cast<size_t>(idx)].id);
        }
    }
    return ids;
}

void LibraryBrowserPanel::DragSourceTable::mouseDrag(const juce::MouseEvent& e)
{
    juce::TableListBox::mouseDrag(e);

    if (e.getDistanceFromDragStart() > 5 && getSelectedRows().size() > 0)
    {
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
        {
            if (!container->isDragAndDropActive())
            {
                auto desc = static_cast<juce::TableListBoxModel*>(getModel())->getDragSourceDescription(getSelectedRows());

                int count = getSelectedRows().size();
                juce::Image dragImg(juce::Image::ARGB, 120, 30, true);
                juce::Graphics g(dragImg);
                g.setColour(Colors::primary.withAlpha(0.85f));
                g.fillRoundedRectangle(0, 0, 120, 30, 8.0f);
                g.setColour(Colors::textBright);
                g.setFont(juce::Font(12.0f, juce::Font::bold));
                g.drawText(juce::String(count) + (count > 1 ? " tracks" : " track"),
                           0, 0, 120, 30, juce::Justification::centred);

                container->startDragging(desc, this, juce::ScaledImage(dragImg), true);
            }
        }
    }
}

}
