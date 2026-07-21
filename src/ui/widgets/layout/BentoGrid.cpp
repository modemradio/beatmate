#include "BentoGrid.h"
#include "../../styles/ColorPalette.h"

namespace BeatMate::UI {

BentoGrid::BentoGrid()
{
    setOpaque(false);
}

void BentoGrid::addCard(juce::Component* card, int colSpan, int rowSpan)
{
    if (card != nullptr)
    {
        cards_.push_back({ card, juce::jmax(1, colSpan), juce::jmax(1, rowSpan) });
        addAndMakeVisible(card);
        resized();
    }
}

void BentoGrid::removeAllCards()
{
    for (auto& c : cards_)
        removeChildComponent(c.component);
    cards_.clear();
}

void BentoGrid::setGap(int gap)
{
    gap_ = gap;
    resized();
}

void BentoGrid::setColumns(int numColumns)
{
    numColumns_ = juce::jmax(1, numColumns);
    resized();
}

void BentoGrid::paint(juce::Graphics& /*g*/)
{
}

void BentoGrid::resized()
{
    layoutCards();
}

void BentoGrid::layoutCards()
{
    if (cards_.empty() || getWidth() <= 0)
        return;

    int totalWidth = getWidth();
    int colWidth = (totalWidth - (numColumns_ - 1) * gap_) / numColumns_;
    int rowHeight = colWidth;

    std::vector<std::vector<bool>> grid;
    int maxRows = 100;
    grid.resize(static_cast<size_t>(maxRows));
    for (auto& row : grid)
        row.resize(static_cast<size_t>(numColumns_), false);

    for (auto& card : cards_)
    {
        int cSpan = juce::jmin(card.colSpan, numColumns_);
        int rSpan = card.rowSpan;

        bool placed = false;
        for (int r = 0; r < maxRows && !placed; ++r)
        {
            for (int c = 0; c <= numColumns_ - cSpan && !placed; ++c)
            {
                bool canPlace = true;
                for (int dr = 0; dr < rSpan && canPlace; ++dr)
                    for (int dc = 0; dc < cSpan && canPlace; ++dc)
                        if (r + dr >= maxRows || grid[static_cast<size_t>(r + dr)][static_cast<size_t>(c + dc)])
                            canPlace = false;

                if (canPlace)
                {
                    for (int dr = 0; dr < rSpan; ++dr)
                        for (int dc = 0; dc < cSpan; ++dc)
                            grid[static_cast<size_t>(r + dr)][static_cast<size_t>(c + dc)] = true;

                    int x = c * (colWidth + gap_);
                    int y = r * (rowHeight + gap_);
                    int w = cSpan * colWidth + (cSpan - 1) * gap_;
                    int h = rSpan * rowHeight + (rSpan - 1) * gap_;

                    card.component->setBounds(x, y, w, h);
                    placed = true;
                }
            }
        }
    }
}

} // namespace BeatMate::UI
