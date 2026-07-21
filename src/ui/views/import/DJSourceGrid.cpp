#include "DJSourceGrid.h"
#include "../../styles/ColorPalette.h"
#include "../../../services/config/I18n.h"

namespace BeatMate::UI {

namespace {
constexpr int kCardW = 220;
constexpr int kCardH = 110;
constexpr int kGap = 14;

juce::Colour brandColour(Services::DJSoftware::DJSoftwareType t)
{
    using T = Services::DJSoftware::DJSoftwareType;
    switch (t)
    {
        case T::Rekordbox: return juce::Colour(0xFF2563EB);
        case T::Serato:    return juce::Colour(0xFF06B6D4);
        case T::Traktor:   return juce::Colour(0xFFF97316);
        case T::VirtualDJ: return juce::Colour(0xFF8B5CF6);
        case T::EngineDJ:  return juce::Colour(0xFF10B981);
        default:           return Colors::primary();
    }
}
}

void DJSourceGrid::setSources(std::vector<Services::DJSoftware::DJSoftwareInfo> sources)
{
    m_sources = std::move(sources);
    m_hovered = -1;
    repaint();
}

juce::Rectangle<int> DJSourceGrid::cardBounds(int index) const
{
    const int cols = juce::jmax(1, (getWidth() + kGap) / (kCardW + kGap));
    const int row = index / cols;
    const int col = index % cols;
    return { col * (kCardW + kGap), row * (kCardH + kGap), kCardW, kCardH };
}

int DJSourceGrid::cardAt(juce::Point<int> pos) const
{
    for (int i = 0; i < static_cast<int>(m_sources.size()); ++i)
        if (cardBounds(i).contains(pos))
            return i;
    return -1;
}

void DJSourceGrid::paint(juce::Graphics& g)
{
    for (int i = 0; i < static_cast<int>(m_sources.size()); ++i)
    {
        const auto& src = m_sources[static_cast<size_t>(i)];
        const auto b = cardBounds(i).toFloat();
        const juce::Colour brand = brandColour(src.type);
        const bool installed = src.isInstalled;
        const bool hovered = (m_hovered == i) && installed;

        g.setColour(hovered ? Colors::bgElevated() : Colors::bgSurface());
        g.fillRoundedRectangle(b, 10.0f);

        g.setColour(brand.withAlpha(installed ? (hovered ? 0.9f : 0.5f) : 0.15f));
        g.drawRoundedRectangle(b.reduced(0.5f), 10.0f, hovered ? 1.6f : 1.0f);

        g.setColour(brand.withAlpha(installed ? 0.85f : 0.25f));
        g.fillRoundedRectangle(b.getX() + 10.0f, b.getY() + 12.0f, 4.0f, 18.0f, 2.0f);

        g.setFont(juce::Font("Segoe UI", 15.0f, juce::Font::bold));
        g.setColour(installed ? Colors::textPrimary() : Colors::textDim());
        g.drawText(juce::String::fromUTF8(src.name.c_str()),
                   static_cast<int>(b.getX()) + 22, static_cast<int>(b.getY()) + 10,
                   static_cast<int>(b.getWidth()) - 32, 22, juce::Justification::centredLeft, true);

        if (!src.version.empty())
        {
            g.setFont(juce::Font(10.5f));
            g.setColour(Colors::textMuted());
            g.drawText("v" + juce::String::fromUTF8(src.version.c_str()),
                       static_cast<int>(b.getX()) + 22, static_cast<int>(b.getY()) + 34,
                       static_cast<int>(b.getWidth()) - 32, 14, juce::Justification::centredLeft, true);
        }

        ProDraw::statusPill(g,
                            installed ? BM_TJ("import.dj.detected") : BM_TJ("import.dj.notDetected"),
                            { b.getX() + 16.0f, b.getBottom() - 34.0f, 130.0f, 22.0f },
                            installed ? Colors::success() : Colors::textDim(), installed);

        if (installed)
        {
            g.setColour(brand.withAlpha(hovered ? 1.0f : 0.6f));
            g.setFont(juce::Font(16.0f, juce::Font::bold));
            g.drawText(juce::CharPointer_UTF8("\xe2\x86\x92"),
                       static_cast<int>(b.getRight()) - 34, static_cast<int>(b.getBottom()) - 36,
                       24, 24, juce::Justification::centred);
        }
    }
}

void DJSourceGrid::mouseMove(const juce::MouseEvent& e)
{
    const int hovered = cardAt(e.getPosition());
    if (hovered != m_hovered)
    {
        m_hovered = hovered;
        const bool clickable = hovered >= 0
            && m_sources[static_cast<size_t>(hovered)].isInstalled;
        setMouseCursor(clickable ? juce::MouseCursor::PointingHandCursor
                                 : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void DJSourceGrid::mouseExit(const juce::MouseEvent&)
{
    if (m_hovered != -1)
    {
        m_hovered = -1;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void DJSourceGrid::mouseUp(const juce::MouseEvent& e)
{
    const int idx = cardAt(e.getPosition());
    if (idx < 0 || idx >= static_cast<int>(m_sources.size()))
        return;
    const auto& src = m_sources[static_cast<size_t>(idx)];
    if (src.isInstalled && onSourceChosen)
        onSourceChosen(src.type);
}

}
