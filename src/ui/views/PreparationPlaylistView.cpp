#include "PreparationPlaylistView.h"

#include "../styles/ColorPalette.h"
#include "../../services/library/TrackDataProvider.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

#include <algorithm>

namespace BeatMate::UI {

namespace {

constexpr int kColDiamondW = 34;
constexpr int kColRankW    = 42;
constexpr int kColBpmW     = 64;
constexpr int kColKeyW     = 56;
constexpr int kRowHeight   = 34;

juce::String formatBpm(double bpm)
{
    if (bpm <= 0.0) return "--";
    return juce::String(bpm, 1);
}

juce::String formatKey(const std::string& k)
{
    if (k.empty()) return "--";
    return juce::String(k);
}

}

PreparationPlaylistView::PreparationPlaylistView()
    : m_provider(nullptr)
{
    setupUI();
}

PreparationPlaylistView::PreparationPlaylistView(Services::Library::TrackDataProvider* provider)
    : m_provider(provider)
{
    setupUI();
}

void PreparationPlaylistView::setupUI()
{
    m_headerLabel = std::make_unique<juce::Label>("header", "PREPARATION PLAYLIST");
    m_headerLabel->setFont(juce::Font("Segoe UI", 13.0f, juce::Font::bold));
    m_headerLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    m_headerLabel->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(*m_headerLabel);

    m_countLabel = std::make_unique<juce::Label>("count", "0 tracks");
    m_countLabel->setFont(juce::Font("Segoe UI", 11.0f, juce::Font::plain));
    m_countLabel->setColour(juce::Label::textColourId, Colors::textSecondary());
    m_countLabel->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*m_countLabel);

    auto setupButton = [this](std::unique_ptr<juce::TextButton>& btn,
                              const juce::String& label,
                              juce::Colour bg)
    {
        btn = std::make_unique<juce::TextButton>(label);
        btn->setColour(juce::TextButton::buttonColourId, bg);
        btn->setColour(juce::TextButton::buttonOnColourId, bg.brighter(0.2f));
        btn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        btn->setColour(juce::TextButton::textColourOnId, Colors::textPrimary());
        btn->addListener(this);
        addAndMakeVisible(*btn);
    };

    setupButton(m_btnPlay,    "Play Next",   Colors::primary());
    setupButton(m_btnRemove,  "Remove",      Colors::bgElevated());
    setupButton(m_btnClear,   "Clear",       Colors::bgElevated());
    setupButton(m_btnAdvance, "Advance >>",  Colors::secondary());

    m_list = std::make_unique<juce::ListBox>("prepList", this);
    m_list->setRowHeight(kRowHeight);
    m_list->setColour(juce::ListBox::backgroundColourId, Colors::bgSurface());
    m_list->setColour(juce::ListBox::outlineColourId, Colors::border());
    m_list->setOutlineThickness(1);
    m_list->setMultipleSelectionEnabled(false);
    addAndMakeVisible(*m_list);
}

void PreparationPlaylistView::addTrack(const Models::Track& track)
{
    m_tracks.push_back(track);
    if (m_diamondIndex < 0 && !m_tracks.empty())
        m_diamondIndex = 0;

    if (m_list) m_list->updateContent();
    if (m_countLabel)
        m_countLabel->setText(juce::String(getNumTracks()) + " tracks",
                              juce::dontSendNotification);
    repaint();
}

void PreparationPlaylistView::removeTrackAt(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tracks.size())) return;
    m_tracks.erase(m_tracks.begin() + index);

    if (m_tracks.empty())
    {
        m_diamondIndex = -1;
    }
    else if (m_diamondIndex >= static_cast<int>(m_tracks.size()))
    {
        m_diamondIndex = static_cast<int>(m_tracks.size()) - 1;
    }
    else if (index < m_diamondIndex)
    {
        --m_diamondIndex;
    }

    if (m_list) m_list->updateContent();
    if (m_countLabel)
        m_countLabel->setText(juce::String(getNumTracks()) + " tracks",
                              juce::dontSendNotification);
    repaint();
}

void PreparationPlaylistView::clearList()
{
    m_tracks.clear();
    m_diamondIndex = -1;
    if (m_list) m_list->updateContent();
    if (m_countLabel)
        m_countLabel->setText("0 tracks", juce::dontSendNotification);
    repaint();
}

void PreparationPlaylistView::setDiamondIndex(int index)
{
    if (m_tracks.empty())
    {
        m_diamondIndex = -1;
    }
    else
    {
        m_diamondIndex = juce::jlimit(0,
            static_cast<int>(m_tracks.size()) - 1, index);
    }
    if (m_list) m_list->repaint();
    repaint();
}

void PreparationPlaylistView::paint(juce::Graphics& g)
{
    const int w = getWidth();
    const int h = getHeight();

    ProDraw::viewBackground(g, w, h);

    auto headerRect = juce::Rectangle<float>(0.0f, 0.0f,
                                              static_cast<float>(w), 44.0f);
    g.setColour(Colors::bgCard().withAlpha(0.65f));
    g.fillRect(headerRect);

    g.setColour(Colors::secondary());
    g.fillRect(0.0f, 0.0f, 3.0f, 44.0f);

    g.setColour(Colors::border());
    g.fillRect(0, 43, w, 1);

    ProDraw::vignette(g, static_cast<float>(w), static_cast<float>(h));
}

void PreparationPlaylistView::resized()
{
    auto area = getLocalBounds();

    auto headerRow = area.removeFromTop(44).reduced(12, 10);
    auto countArea = headerRow.removeFromRight(120);
    if (m_countLabel)  m_countLabel->setBounds(countArea);
    if (m_headerLabel) m_headerLabel->setBounds(headerRow);

    auto toolbar = area.removeFromBottom(52).reduced(10, 8);
    const int btnW = 110;
    const int btnH = toolbar.getHeight();
    const int gap  = 8;

    int x = toolbar.getX();
    if (m_btnPlay)    { m_btnPlay->setBounds(x, toolbar.getY(), btnW, btnH);    x += btnW + gap; }
    if (m_btnAdvance) { m_btnAdvance->setBounds(x, toolbar.getY(), btnW, btnH); x += btnW + gap; }
    if (m_btnRemove)  { m_btnRemove->setBounds(x, toolbar.getY(), btnW, btnH);  x += btnW + gap; }
    if (m_btnClear)   { m_btnClear->setBounds(x, toolbar.getY(), btnW, btnH);   x += btnW + gap; }

    if (m_list) m_list->setBounds(area.reduced(8, 4));
}

int PreparationPlaylistView::getNumRows()
{
    return static_cast<int>(m_tracks.size());
}

void PreparationPlaylistView::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                                int width, int height, bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= static_cast<int>(m_tracks.size()))
        return;

    const auto& t = m_tracks[rowNumber];
    const bool isDiamond = (rowNumber == m_diamondIndex);

    juce::Colour rowBg;
    if (rowIsSelected)
        rowBg = Colors::primary().withAlpha(0.22f);
    else if (isDiamond)
        rowBg = Colors::secondary().withAlpha(0.14f);
    else if (rowNumber % 2 == 0)
        rowBg = Colors::bgCard();
    else
        rowBg = Colors::bgSurface();

    g.setColour(rowBg);
    g.fillRect(0, 0, width, height);

    if (isDiamond)
    {
        g.setColour(Colors::secondary());
        g.fillRect(0, 0, 3, height);
    }

    int x = 6;

    if (isDiamond)
    {
        auto diamondArea = juce::Rectangle<float>(
            static_cast<float>(x),
            height * 0.5f - 8.0f,
            16.0f, 16.0f);
        drawDiamond(g, diamondArea, Colors::secondary());
    }
    x += kColDiamondW;

    g.setColour(isDiamond ? Colors::secondary() : Colors::textSecondary());
    g.setFont(juce::Font("Segoe UI", 11.0f, juce::Font::bold));
    g.drawText("#" + juce::String(rowNumber + 1),
               x, 0, kColRankW, height,
               juce::Justification::centredLeft);
    x += kColRankW;

    const int rightReserved = kColBpmW + kColKeyW + 12;
    const int middleW = width - x - rightReserved;

    g.setColour(Colors::textPrimary());
    g.setFont(juce::Font("Segoe UI", 13.0f, juce::Font::bold));
    g.drawText(juce::String(t.title.empty() ? "(Untitled)" : t.title),
               x, 2, middleW, height / 2,
               juce::Justification::centredLeft, true);

    g.setColour(Colors::textSecondary());
    g.setFont(juce::Font("Segoe UI", 11.0f, juce::Font::plain));
    g.drawText(juce::String(t.artist.empty() ? "Unknown artist" : t.artist),
               x, height / 2, middleW, height / 2 - 2,
               juce::Justification::centredLeft, true);

    int rx = width - rightReserved + 6;
    ProDraw::badge(g, formatBpm(t.bpm),
                   static_cast<float>(rx),
                   (height - 20) * 0.5f,
                   static_cast<float>(kColBpmW - 8), 20.0f,
                   Colors::bpmBadge());
    rx += kColBpmW;

    ProDraw::badge(g, formatKey(t.key),
                   static_cast<float>(rx),
                   (height - 20) * 0.5f,
                   static_cast<float>(kColKeyW - 8), 20.0f,
                   Colors::keyBadge());

    g.setColour(Colors::border().withAlpha(0.4f));
    g.fillRect(0, height - 1, width, 1);
}

void PreparationPlaylistView::listBoxItemClicked(int /*row*/, const juce::MouseEvent& /*e*/)
{
}

void PreparationPlaylistView::listBoxItemDoubleClicked(int row, const juce::MouseEvent& /*e*/)
{
    firePreviewForIndex(row);
}

void PreparationPlaylistView::deleteKeyPressed(int lastRowSelected)
{
    removeTrackAt(lastRowSelected);
}

bool PreparationPlaylistView::keyPressed(const juce::KeyPress& key)
{
    using K = juce::KeyPress;
    if (m_tracks.empty()) return false;

    if (key == K(K::upKey)) {
        const int cur = (m_diamondIndex >= 0) ? m_diamondIndex : 0;
        setDiamondIndex(std::max(0, cur - 1));
        m_listeners.call([this](Listener& l) { l.preparationDiamondAdvanced(m_diamondIndex); });
        return true;
    }
    if (key == K(K::downKey)) {
        const int cur = (m_diamondIndex >= 0) ? m_diamondIndex : -1;
        const int maxIdx = (int)m_tracks.size() - 1;
        setDiamondIndex(std::min(maxIdx, cur + 1));
        m_listeners.call([this](Listener& l) { l.preparationDiamondAdvanced(m_diamondIndex); });
        return true;
    }
    if (key == K(K::returnKey)) {
        firePreviewForIndex(m_diamondIndex);
        return true;
    }
    if (key == K(K::insertKey)) {
        if (m_list) {
            const int selected = m_list->getSelectedRow();
            if (selected >= 0) {
                setDiamondIndex(selected);
                m_listeners.call([this](Listener& l) { l.preparationDiamondAdvanced(m_diamondIndex); });
                return true;
            }
        }
    }
    return false;
}

void PreparationPlaylistView::buttonClicked(juce::Button* b)
{
    if (b == m_btnPlay.get())
    {
        firePreviewForIndex(m_diamondIndex);
    }
    else if (b == m_btnRemove.get())
    {
        if (m_list)
        {
            int sel = m_list->getSelectedRow();
            if (sel >= 0) removeTrackAt(sel);
        }
    }
    else if (b == m_btnClear.get())
    {
        clearList();
    }
    else if (b == m_btnAdvance.get())
    {
        advanceDiamond();
    }
}

void PreparationPlaylistView::drawDiamond(juce::Graphics& g,
                                           juce::Rectangle<float> area,
                                           juce::Colour color) const
{
    const float cx = area.getCentreX();
    const float cy = area.getCentreY();
    const float halfW = area.getWidth()  * 0.5f;
    const float halfH = area.getHeight() * 0.5f;

    juce::Path diamond;
    diamond.startNewSubPath(cx,         cy - halfH);
    diamond.lineTo        (cx + halfW,  cy);
    diamond.lineTo        (cx,          cy + halfH);
    diamond.lineTo        (cx - halfW,  cy);
    diamond.closeSubPath();

    g.setColour(color.withAlpha(0.25f));
    g.fillPath(diamond, juce::AffineTransform::scale(1.25f, 1.25f, cx, cy));

    g.setColour(color);
    g.fillPath(diamond);

    g.setColour(color.brighter(0.4f));
    g.strokePath(diamond, juce::PathStrokeType(1.2f));
}

void PreparationPlaylistView::firePreviewForIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tracks.size())) return;
    const juce::String path(m_tracks[index].filePath);
    m_listeners.call([&](Listener& l) { l.preparationTrackPreviewRequested(path); });
}

void PreparationPlaylistView::advanceDiamond()
{
    if (m_tracks.empty())
    {
        m_diamondIndex = -1;
        return;
    }
    int newIdx = m_diamondIndex + 1;
    if (newIdx >= static_cast<int>(m_tracks.size()))
        newIdx = static_cast<int>(m_tracks.size()) - 1;

    if (newIdx != m_diamondIndex)
    {
        m_diamondIndex = newIdx;
        if (m_list) m_list->repaint();
        m_listeners.call([newIdx](Listener& l) { l.preparationDiamondAdvanced(newIdx); });
    }
    repaint();
}

}
