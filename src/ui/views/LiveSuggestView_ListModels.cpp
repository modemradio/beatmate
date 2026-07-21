
#include "LiveSuggestView.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"

#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>

namespace BeatMate::UI {

namespace {

void paintRowBase(juce::Graphics& g, int width, int height,
                  int rowNumber, bool rowIsSelected, bool rowIsHovered,
                  juce::Colour accent)
{
    if (rowIsSelected)
    {
        g.setColour(accent.withAlpha(0.13f));
        g.fillRect(0, 0, width, height);
        g.setColour(accent.withAlpha(0.85f));
        g.fillRoundedRectangle(0.0f, 3.0f, 3.0f, static_cast<float>(height - 6), 1.5f);
    }
    else if (rowIsHovered)
    {
        g.setColour(Colors::glassHover());
        g.fillRect(0, 0, width, height);
        g.setColour(accent.withAlpha(0.35f));
        g.fillRoundedRectangle(0.0f, 3.0f, 2.0f, static_cast<float>(height - 6), 1.0f);
    }
    else if (rowNumber % 2 == 0)
    {
        g.setColour(Colors::glassWhite());
        g.fillRect(0, 0, width, height);
    }

    g.setColour(Colors::border().withAlpha(0.35f));
    g.fillRect(Spacing::md, height - 1, width - Spacing::md * 2, 1);
}

void paintHoverChevron(juce::Graphics& g, int width, int height, juce::Colour accent)
{
    g.setColour(accent.withAlpha(0.7f));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(14.0f).withStyle("Bold")));
    g.drawText(juce::String::fromUTF8("\xe2\x80\xba"),
               width - 18, 0, 12, height, juce::Justification::centred);
}

juce::String formatBpm(double bpm)
{
    return bpm > 0.0 ? juce::String(bpm, 1) : juce::String::fromUTF8("\xe2\x80\x94");
}

juce::Rectangle<int> suggestionSendZone(int width, int height)
{
    return { width - 78, (height - 24) / 2, 68, 24 };
}

juce::Rectangle<int> cabinCardRect(int width, int height, int col)
{
    const int half = width / 2;
    return col == 0 ? juce::Rectangle<int>(3, 3, half - 6, height - 6)
                    : juce::Rectangle<int>(half + 3, 3, half - 6, height - 6);
}

juce::Rectangle<int> cabinSendZone(const juce::Rectangle<int>& card)
{
    return { card.getRight() - 84, card.getY() + 5, 76, 26 };
}

void paintSendButton(juce::Graphics& g, const juce::Rectangle<int>& zone)
{
    g.setColour(Colors::primary().withAlpha(0.92f));
    g.fillRoundedRectangle(zone.toFloat(), 6.0f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f).withStyle("Bold")));
    g.drawText(BM_TJ("live.btn.send"), zone, juce::Justification::centred);
}

}

int BeatMateLiveView::SuggestionListModel::getNumRows()
{
    if (!entries) return 0;
    const int n = static_cast<int>(entries->size());
    return cabin ? (n + 1) / 2 : n;
}

int BeatMateLiveView::SuggestionListModel::entryIndexForEvent(int row, const juce::MouseEvent& e) const
{
    if (!entries) return -1;
    int idx = row;
    if (cabin)
    {
        const int w = e.eventComponent != nullptr ? e.eventComponent->getWidth() : 0;
        idx = row * 2 + ((w > 0 && e.getPosition().x >= w / 2) ? 1 : 0);
    }
    return (idx >= 0 && idx < static_cast<int>(entries->size())) ? idx : -1;
}

void BeatMateLiveView::SuggestionListModel::paintListBoxItem(
    int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (!entries || rowNumber < 0) return;

    if (cabin)
    {
        const bool hoveredCabin = (rowNumber == hoveredRow);
        for (int col = 0; col < 2; ++col)
        {
            const int idx = rowNumber * 2 + col;
            if (idx >= static_cast<int>(entries->size())) break;
            const auto& en = (*entries)[static_cast<size_t>(idx)];
            const auto card = cabinCardRect(width, height, col);
            const auto cf = card.toFloat();

            g.setColour(juce::Colour(0xFF11161F));
            g.fillRoundedRectangle(cf, 10.0f);
            g.setColour((rowIsSelected || hoveredCabin)
                            ? Colors::primary().withAlpha(0.8f)
                            : Colors::borderLight());
            g.drawRoundedRectangle(cf.reduced(0.7f), 10.0f, hoveredCabin ? 1.6f : 1.0f);

            auto inner = card.reduced(12, 6);

            auto titleRow = inner.removeFromTop(26);
            g.setColour(en.alreadyPlayed ? juce::Colours::white.withAlpha(0.45f)
                                         : juce::Colours::white);
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(18.0f).withStyle("Bold")));
            g.drawText(en.title, titleRow.withTrimmedRight(96), juce::Justification::centredLeft, true);

            auto artistRow = inner.removeFromTop(18);
            g.setColour(juce::Colour(0xFFB8C2D0));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f)));
            g.drawText(en.artist, artistRow, juce::Justification::centredLeft, true);

            auto badgeRow = inner.removeFromBottom(28);
            const float score = en.score;
            juce::Colour scoreCol = score >= 0.85f ? Colors::success()
                                  : score >= 0.6f  ? Colors::warning()
                                  : Colors::textMuted();
            auto scoreRect = badgeRow.removeFromRight(72);
            const float keyRight = static_cast<float>(badgeRow.getX()) + 112.0f + 66.0f;
            const float scoreLeft = static_cast<float>(scoreRect.getX());
            ProDraw::badge(g, formatBpm(en.bpm) + " BPM",
                           static_cast<float>(badgeRow.getX()), static_cast<float>(badgeRow.getY()),
                           104.0f, 28.0f, Colors::bpmBadge());
            if (en.key.isNotEmpty() && keyRight <= scoreLeft - 6.0f)
                ProDraw::badge(g, en.key,
                               static_cast<float>(badgeRow.getX()) + 112.0f,
                               static_cast<float>(badgeRow.getY()),
                               66.0f, 28.0f, Colors::keyBadge());

            g.setColour(scoreCol);
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(22.0f).withStyle("Bold")));
            g.drawText(juce::String(static_cast<int>(score * 100.0f)) + "%",
                       scoreRect, juce::Justification::centredRight);

            paintSendButton(g, cabinSendZone(card));
        }
        return;
    }

    if (rowNumber >= static_cast<int>(entries->size()))
        return;

    const auto& entry = (*entries)[static_cast<size_t>(rowNumber)];
    const bool hovered = (rowNumber == hoveredRow);

    paintRowBase(g, width, height, rowNumber, rowIsSelected, hovered, Colors::primary());

    if (entry.relevance > 0.0f)
    {
        const float barW = static_cast<float>(width - Spacing::md * 2) * juce::jlimit(0.0f, 1.0f, entry.relevance);
        auto barBounds = juce::Rectangle<float>(static_cast<float>(Spacing::md),
                                                static_cast<float>(height - 4),
                                                barW, 2.0f);
        juce::ColourGradient grad(Colors::primary(), barBounds.getX(), barBounds.getCentreY(),
                                  Colors::success(), barBounds.getRight(), barBounds.getCentreY(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(barBounds, 1.0f);
    }

    g.setColour(hovered ? Colors::textSecondary() : Colors::textDim());
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f).withStyle("Bold")));
    g.drawText(juce::String(rowNumber + 1), 4, 0, 22, height, juce::Justification::centred);

    int xOffset = 30;
    const int rightColW = 196;

    if (entry.score > 0.9f)
    {
        ProDraw::badge(g, "PERFECT", static_cast<float>(xOffset), 5.0f, 62.0f, 15.0f, Colors::success());
        xOffset += 68;
    }
    else if (entry.score > 0.7f)
    {
        ProDraw::badge(g, "GOOD", static_cast<float>(xOffset), 5.0f, 46.0f, 15.0f, Colors::warning());
        xOffset += 52;
    }

    if (entry.inLibrary)
    {
        ProDraw::badge(g, "BIBLIO", static_cast<float>(xOffset), 5.0f, 52.0f, 15.0f, Colors::accent());
        xOffset += 58;
    }

    const int textW = width - rightColW - xOffset;
    g.setColour(entry.alreadyPlayed ? Colors::textPrimary().withAlpha(0.45f)
                                    : Colors::textPrimary());
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.5f).withStyle("Bold")));
    g.drawText(entry.title, xOffset, 4, textW, height / 2 - 4,
               juce::Justification::centredLeft, true);

    g.setColour(Colors::textSecondary());
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    juce::String sub = entry.artist;
    if (entry.reason.isNotEmpty())
        sub << juce::String::fromUTF8("  \xc2\xb7  ") << entry.reason;
    g.drawText(sub, 30, height / 2, width - rightColW - 30, height / 2 - 6,
               juce::Justification::centredLeft, true);

    const float badgeY = (static_cast<float>(height) - 20.0f) * 0.5f;
    ProDraw::badge(g, formatBpm(entry.bpm), static_cast<float>(width - 188), badgeY, 56.0f, 20.0f,
                   Colors::bpmBadge());
    if (entry.key.isNotEmpty())
        ProDraw::badge(g, entry.key, static_cast<float>(width - 126), badgeY, 42.0f, 20.0f,
                       Colors::keyBadge());

    if (entry.streamingSource.isNotEmpty() && entry.streamingSource != "local")
    {
        juce::String src = entry.streamingSource == "streaming"
                               ? juce::String("STREAM")
                               : entry.streamingSource.toUpperCase();
        ProDraw::badge(g, src, static_cast<float>(width - 188),
                       static_cast<float>(height - 19), 56.0f, 13.0f, Colors::accent());
    }

    if (entry.chartNew)
        ProDraw::badge(g, "NEW", static_cast<float>(width - 126),
                       static_cast<float>(height - 19), 42.0f, 13.0f, Colors::accent());
    else if (entry.chartDelta > 0)
        ProDraw::badge(g, juce::String::fromUTF8("\xe2\x86\x91") + juce::String(entry.chartDelta),
                       static_cast<float>(width - 126),
                       static_cast<float>(height - 19), 42.0f, 13.0f, Colors::success());
    else if (entry.chartPos > 0)
        ProDraw::badge(g, "#" + juce::String(entry.chartPos), static_cast<float>(width - 126),
                       static_cast<float>(height - 19), 42.0f, 13.0f, Colors::textSecondary());

    {
        const float score = entry.score;
        juce::Colour scoreCol = score >= 0.85f ? Colors::success()
                              : score >= 0.6f  ? Colors::warning()
                              : Colors::textMuted();
        g.setColour(scoreCol);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f).withStyle("Bold")));
        g.drawText(juce::String(static_cast<int>(score * 100.0f)) + "%",
                   width - 78, 0, 54, height, juce::Justification::centredRight);
    }

    if (hovered)
        paintSendButton(g, suggestionSendZone(width, height));
}

void BeatMateLiveView::SuggestionListModel::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    const int idx = entryIndexForEvent(row, e);
    if (idx < 0) return;

    if (e.mods.isRightButtonDown())
    {
        if (onItemClicked) onItemClicked(idx, e);
        return;
    }

    const int w = e.eventComponent != nullptr ? e.eventComponent->getWidth() : 0;
    const int h = e.eventComponent != nullptr ? e.eventComponent->getHeight() : 0;
    const auto pos = e.getPosition();

    if (cabin && w > 0)
    {
        const int col = (pos.x >= w / 2) ? 1 : 0;
        const auto card = cabinCardRect(w, h, col);
        if (cabinSendZone(card).contains(pos)) { if (onSendClicked) onSendClicked(idx); return; }
    }
    else if (w > 0)
    {
        if (suggestionSendZone(w, h).contains(pos)) { if (onSendClicked) onSendClicked(idx); return; }
    }

    if (onItemClicked) onItemClicked(idx, e);
}

void BeatMateLiveView::SuggestionListModel::listBoxItemDoubleClicked(int row, const juce::MouseEvent& e)
{
    const int idx = entryIndexForEvent(row, e);
    if (idx >= 0 && onItemDoubleClicked) onItemDoubleClicked(idx);
}

int BeatMateLiveView::HistoryListModel::getNumRows()
{
    return entries ? static_cast<int>(entries->size()) : 0;
}

void BeatMateLiveView::HistoryListModel::paintListBoxItem(
    int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (!entries || rowNumber < 0 || rowNumber >= static_cast<int>(entries->size()))
        return;

    const auto& entry = (*entries)[static_cast<size_t>(rowNumber)];
    const bool hovered = (rowNumber == hoveredRow);

    paintRowBase(g, width, height, rowNumber, rowIsSelected, hovered, Colors::secondary());

    const int dotX = 18;
    const int dotY = height / 2;
    if (rowNumber > 0)
    {
        g.setColour(Colors::secondary().withAlpha(0.25f));
        g.drawVerticalLine(dotX, 0.0f, static_cast<float>(dotY - 5));
    }
    if (rowNumber < getNumRows() - 1)
    {
        g.setColour(Colors::secondary().withAlpha(0.25f));
        g.drawVerticalLine(dotX, static_cast<float>(dotY + 5), static_cast<float>(height));
    }
    g.setColour(Colors::secondary().withAlpha(0.25f));
    g.fillEllipse(static_cast<float>(dotX - 6), static_cast<float>(dotY - 6), 12.0f, 12.0f);
    g.setColour(rowNumber == 0 ? Colors::secondary() : Colors::secondary().withAlpha(0.75f));
    g.fillEllipse(static_cast<float>(dotX - 3.5f), static_cast<float>(dotY - 3.5f), 7.0f, 7.0f);

    g.setColour(Colors::textMuted());
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f).withStyle("Bold")));
    g.drawText(entry.timestamp, 32, 0, 44, height, juce::Justification::centredLeft);

    const int rightColW = 150;
    g.setColour(Colors::textPrimary());
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f).withStyle("Bold")));
    g.drawText(entry.title, 84, 4, width - rightColW - 84, height / 2 - 4,
               juce::Justification::centredLeft, true);

    g.setColour(Colors::textSecondary());
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.5f)));
    juce::String sub = entry.artist;
    if (entry.durationPlayed.isNotEmpty() && entry.durationPlayed != "0:00")
        sub << juce::String::fromUTF8("  \xc2\xb7  ") << entry.durationPlayed;
    g.drawText(sub, 84, height / 2, width - rightColW - 84, height / 2 - 6,
               juce::Justification::centredLeft, true);

    const float badgeY = (static_cast<float>(height) - 18.0f) * 0.5f;
    ProDraw::badge(g, formatBpm(entry.bpm), static_cast<float>(width - 142), badgeY, 54.0f, 18.0f,
                   Colors::bpmBadge());
    if (entry.key.isNotEmpty())
        ProDraw::badge(g, entry.key, static_cast<float>(width - 82), badgeY, 40.0f, 18.0f,
                       Colors::keyBadge());

    if (hovered)
        paintSendButton(g, suggestionSendZone(width, height));
}

void BeatMateLiveView::HistoryListModel::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (row < 0 || e.mods.isRightButtonDown()) return;
    const int w = e.eventComponent != nullptr ? e.eventComponent->getWidth() : 0;
    const int h = e.eventComponent != nullptr ? e.eventComponent->getHeight() : 0;
    if (w > 0 && suggestionSendZone(w, h).contains(e.getPosition()))
    {
        if (onSendClicked) onSendClicked(row);
    }
}

void BeatMateLiveView::HistoryListModel::listBoxItemDoubleClicked(int row, const juce::MouseEvent& /*e*/)
{
    if (row >= 0 && onItemDoubleClicked) onItemDoubleClicked(row);
}

int BeatMateLiveView::TrendingListModel::getNumRows()
{
    return entries ? static_cast<int>(entries->size()) : 0;
}

void BeatMateLiveView::TrendingListModel::paintListBoxItem(
    int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (!entries || rowNumber < 0 || rowNumber >= static_cast<int>(entries->size()))
        return;

    const auto& entry = (*entries)[static_cast<size_t>(rowNumber)];
    const float hf = static_cast<float>(height);
    const bool hovered = (rowNumber == hoveredRow);
    const bool isTop3 = (rowNumber < 3 && entry.position >= 1 && entry.position <= 3);

    juce::Colour medalCol = entry.position == 1 ? juce::Colour(0xFFFFD700)
                          : entry.position == 2 ? juce::Colour(0xFFC0C0C0)
                          : juce::Colour(0xFFCD7F32);

    paintRowBase(g, width, height, rowNumber, rowIsSelected, hovered,
                 isTop3 ? medalCol : Colors::primary());

    if (entry.position <= 0)
    {
        g.setColour(Colors::textSecondary());
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.5f).withStyle("Bold")));
        g.drawText(entry.title, Spacing::md, 2, width - Spacing::md * 2, height / 2,
                   juce::Justification::centredLeft, true);
        g.setColour(Colors::textMuted());
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.5f)));
        g.drawText(entry.artist, Spacing::md, height / 2, width - Spacing::md * 2, height / 2 - 4,
                   juce::Justification::centredLeft, true);
        return;
    }

    if (!rowIsSelected && !hovered && isTop3)
    {
        g.setColour(medalCol.withAlpha(0.05f));
        g.fillRect(0, 0, width, height);
    }

    if (entry.trendScore > 0.0f)
    {
        const float barW = (static_cast<float>(width) - 60.0f) * juce::jlimit(0.0f, 1.0f, entry.trendScore);
        juce::ColourGradient barGrad(Colors::primary(), 48.0f, hf - 3.0f,
                                     Colors::success(), 48.0f + barW, hf - 3.0f, false);
        g.setGradientFill(barGrad);
        g.fillRoundedRectangle(48.0f, hf - 4.0f, barW, 2.0f, 1.0f);
    }

    if (isTop3)
    {
        g.setColour(medalCol.withAlpha(0.18f));
        g.fillEllipse(9.0f, (hf - 26.0f) * 0.5f, 26.0f, 26.0f);
        g.setColour(medalCol);
        g.fillEllipse(11.0f, (hf - 22.0f) * 0.5f, 22.0f, 22.0f);
        g.setColour(juce::Colour(0xFF14141C));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f).withStyle("Bold")));
        g.drawText(juce::String(entry.position), 11, static_cast<int>((hf - 22.0f) * 0.5f), 22, 22,
                   juce::Justification::centred);
    }
    else
    {
        g.setColour(hovered ? Colors::textSecondary() : Colors::textDim());
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f).withStyle("Bold")));
        g.drawText(entry.position > 0 ? juce::String(entry.position) : juce::String(),
                   8, 0, 32, height, juce::Justification::centred);
    }

    const int rightColW = 286;
    int xOffset = 44;
    if (entry.inLibrary)
    {
        ProDraw::badge(g, "BIBLIO", static_cast<float>(xOffset), 4.0f, 52.0f, 15.0f, Colors::accent());
        xOffset += 58;
    }
    g.setColour(Colors::textPrimary());
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.5f).withStyle("Bold")));
    g.drawText(entry.title, xOffset, 4, width - rightColW - xOffset, height / 2 - 4,
               juce::Justification::centredLeft, true);

    g.setColour(Colors::textSecondary());
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.5f)));
    juce::String subLine = entry.artist;
    if (entry.genre.isNotEmpty())
        subLine << juce::String::fromUTF8("  \xc2\xb7  ") << entry.genre;
    g.drawText(subLine, 44, height / 2, width - rightColW - 44, height / 2 - 6,
               juce::Justification::centredLeft, true);

    if (entry.energy > 0.0f)
    {
        const float energyW = 40.0f;
        const float energyH = 5.0f;
        const float energyX = static_cast<float>(width - 270);
        const float energyY = (hf - energyH) * 0.5f;
        g.setColour(Colors::bgElevated());
        g.fillRoundedRectangle(energyX, energyY, energyW, energyH, 2.5f);
        const float fillW = energyW * std::min(1.0f, entry.energy / 10.0f);
        juce::Colour energyCol = entry.energy > 7.0f ? Colors::energyHigh()
                               : entry.energy > 4.0f ? Colors::energyMedium()
                               : Colors::success();
        g.setColour(energyCol);
        g.fillRoundedRectangle(energyX, energyY, fillW, energyH, 2.5f);
    }

    const float badgeY = (hf - 19.0f) * 0.5f;
    ProDraw::badge(g, formatBpm(entry.bpm), static_cast<float>(width - 220), badgeY, 56.0f, 19.0f,
                   Colors::bpmBadge());
    if (entry.key.isNotEmpty())
        ProDraw::badge(g, entry.key, static_cast<float>(width - 158), badgeY, 42.0f, 19.0f,
                       Colors::keyBadge());

    if (entry.isNew || entry.delta != 0 || entry.prevPosition > 0)
    {
        juce::String deltaTxt;
        juce::Colour deltaCol = Colors::textMuted();
        if (entry.isNew)
        {
            deltaTxt = "NEW";
            deltaCol = Colors::accent();
        }
        else if (entry.delta > 0)
        {
            deltaTxt = juce::String::fromUTF8("\xe2\x86\x91") + juce::String(entry.delta);
            deltaCol = Colors::success();
        }
        else if (entry.delta < 0)
        {
            deltaTxt = juce::String::fromUTF8("\xe2\x86\x93") + juce::String(-entry.delta);
            deltaCol = Colors::error();
        }
        else
        {
            deltaTxt = "=";
        }
        ProDraw::badge(g, deltaTxt, static_cast<float>(width - 104), badgeY, 44.0f, 19.0f, deltaCol);
    }

    if (entry.source.isNotEmpty())
    {
        g.setColour(Colors::textMuted());
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(8.5f)));
        g.drawText(entry.source, width - 130, height - 15, 106, 12,
                   juce::Justification::centredRight);
    }
    else if (entry.playCount > 0)
    {
        g.setColour(Colors::textMuted());
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.5f)));
        g.drawText(juce::String(entry.playCount),
                   width - 108, 0, 84, height, juce::Justification::centredRight);
    }

    if (hovered)
        paintSendButton(g, suggestionSendZone(width, height));
}

void BeatMateLiveView::TrendingListModel::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (row < 0) return;
    if (e.mods.isRightButtonDown() || e.mods.isCtrlDown())
    {
        if (onOpenSendMenu) onOpenSendMenu(row);
        return;
    }
    const int w = e.eventComponent != nullptr ? e.eventComponent->getWidth() : 0;
    const int h = e.eventComponent != nullptr ? e.eventComponent->getHeight() : 0;
    if (w > 0 && suggestionSendZone(w, h).contains(e.getPosition()))
    {
        if (onSendClicked) onSendClicked(row);
    }
}

void BeatMateLiveView::TrendingListModel::listBoxItemDoubleClicked(int row, const juce::MouseEvent& /*e*/)
{
    if (row >= 0 && onQuickSend) onQuickSend(row);
}

}
