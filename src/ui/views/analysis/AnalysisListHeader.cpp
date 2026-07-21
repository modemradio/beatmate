#include "AnalysisListHeader.h"
#include "../../../services/config/I18n.h"

namespace BeatMate::UI {

AnalysisListHeader::AnalysisListHeader()
{
    setInterceptsMouseClicks(true, false);
}

void AnalysisListHeader::setSortState(AnalysisColumns::Col col, bool ascending)
{
    m_sortColumn = col;
    m_sortAscending = ascending;
    repaint();
}

void AnalysisListHeader::retranslateUi()
{
    repaint();
}

void AnalysisListHeader::paint(juce::Graphics& g)
{
    const int w = getWidth();
    const int h = getHeight();

    g.setColour(Colors::bgCard().brighter(0.03f));
    g.fillRoundedRectangle(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 6.0f);
    g.setColour(Colors::glassBorder());
    g.drawRoundedRectangle(0.5f, 0.5f, static_cast<float>(w) - 1.0f, static_cast<float>(h) - 1.0f, 6.0f, 0.8f);

    g.setFont(Type::label());
    for (const auto& sp : AnalysisColumns::specs()) {
        if (sp.col == AnalysisColumns::Col::Check)
            continue;
        auto bounds = AnalysisColumns::columnBounds(sp.col, w, h);

        if (sp.sortable && sp.col == m_hoverColumn) {
            g.setColour(Colors::glassHover());
            g.fillRoundedRectangle(bounds.toFloat().reduced(2.0f, 4.0f), 4.0f);
        }

        const bool isSorted = (sp.col == m_sortColumn);
        g.setColour(isSorted ? Colors::textPrimary() : Colors::textMuted());
        auto textBounds = bounds.reduced(8, 0);
        g.drawText(BM_TJ(sp.i18nKey), textBounds, sp.justification, true);

        if (isSorted) {
            const float cx = (sp.justification == juce::Justification::centredLeft)
                ? static_cast<float>(textBounds.getX())
                    + juce::GlyphArrangement::getStringWidth(Type::label(), BM_TJ(sp.i18nKey)) + 10.0f
                : static_cast<float>(bounds.getRight() - 14);
            const float cy = static_cast<float>(h) * 0.5f;
            juce::Path arrow;
            if (m_sortAscending)
                arrow.addTriangle(cx - 3.5f, cy + 2.0f, cx + 3.5f, cy + 2.0f, cx, cy - 3.0f);
            else
                arrow.addTriangle(cx - 3.5f, cy - 2.0f, cx + 3.5f, cy - 2.0f, cx, cy + 3.0f);
            g.setColour(Colors::primary());
            g.fillPath(arrow);
        }
    }
}

void AnalysisListHeader::mouseMove(const juce::MouseEvent& e)
{
    const auto col = AnalysisColumns::columnAt(e.x, getWidth());
    if (col != m_hoverColumn) {
        m_hoverColumn = col;
        repaint();
    }
}

void AnalysisListHeader::mouseExit(const juce::MouseEvent&)
{
    m_hoverColumn = AnalysisColumns::Col::Count;
    repaint();
}

void AnalysisListHeader::mouseDown(const juce::MouseEvent& e)
{
    const auto col = AnalysisColumns::columnAt(e.x, getWidth());
    if (col == AnalysisColumns::Col::Count || col == AnalysisColumns::Col::Check)
        return;
    for (const auto& sp : AnalysisColumns::specs())
        if (sp.col == col && sp.sortable && onSortRequested)
            onSortRequested(col);
}

} // namespace BeatMate::UI
