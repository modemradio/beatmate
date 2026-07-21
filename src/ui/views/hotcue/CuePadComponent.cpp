#include "CuePadComponent.h"
#include "../../styles/ColorPalette.h"

namespace BeatMate::UI {

CuePadComponent::CuePadComponent(int index) : m_index(index)
{
    setMouseClickGrabsKeyboardFocus(false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void CuePadComponent::setInfo(const PadInfo& info)
{
    m_info = info;
    repaint();
}

void CuePadComponent::setSelected(bool selected)
{
    if (m_selected == selected) return;
    m_selected = selected;
    repaint();
}

void CuePadComponent::setHolding(bool holding)
{
    if (m_holding == holding) return;
    m_holding = holding;
    repaint();
}

void CuePadComponent::setPreviewing(bool previewing)
{
    if (m_previewing == previewing) return;
    m_previewing = previewing;
    repaint();
}

void CuePadComponent::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced(1.0f);
    const float radius = 8.0f;
    const juce::String letter = juce::String::charToString(
        static_cast<juce::juce_wchar>('A' + m_index));

    if (m_info.active)
    {
        const juce::Colour base = m_info.color;

        g.setColour(base.withAlpha(m_holding || m_previewing ? 0.30f : 0.12f));
        g.fillRoundedRectangle(b.expanded(3.0f), radius + 2.0f);

        float topA = m_pressed ? 0.95f : (m_hovered ? 0.92f : 0.85f);
        float botA = m_pressed ? 0.75f : (m_hovered ? 0.65f : 0.55f);
        juce::ColourGradient grad(base.withAlpha(topA), b.getX(), b.getY(),
                                  base.withAlpha(botA), b.getX(), b.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(b, radius);

        g.setColour(juce::Colour(0x30FFFFFF));
        g.fillRoundedRectangle(b.getX() + 3.0f, b.getY() + 2.0f, b.getWidth() - 6.0f, 2.5f, 1.5f);

        g.setColour(m_holding ? Colors::success() : base.brighter(0.35f));
        g.drawRoundedRectangle(b, radius, m_holding ? 2.0f : 1.2f);

        const int pad = 8;
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.setFont(juce::Font("Segoe UI", 17.0f, juce::Font::bold));
        g.drawText(letter, pad + 1, 5, 24, 20, juce::Justification::centredLeft);
        g.setColour(juce::Colours::white);
        g.drawText(letter, pad, 4, 24, 20, juce::Justification::centredLeft);

        g.setFont(juce::Font("Consolas", 11.0f, juce::Font::bold));
        g.setColour(juce::Colours::white.withAlpha(0.92f));
        g.drawText(m_info.timeText, pad, getHeight() - 20, getWidth() - pad * 2, 14,
                   juce::Justification::bottomLeft);

        if (m_info.name.isNotEmpty())
        {
            g.setFont(juce::Font("Segoe UI", 11.5f, juce::Font::bold));
            g.setColour(juce::Colours::white);
            g.drawText(m_info.name, pad + 26, 4, getWidth() - pad * 2 - 26, 20,
                       juce::Justification::centredRight, true);
        }
    }
    else
    {
        g.setColour(m_hovered ? Colors::bgElevated() : Colors::bgSurface());
        g.fillRoundedRectangle(b, radius);
        g.setColour(m_info.color.withAlpha(m_hovered ? 0.55f : 0.30f));
        g.drawRoundedRectangle(b, radius, 1.0f);

        g.setColour(m_info.color.withAlpha(m_hovered ? 0.85f : 0.45f));
        g.setFont(juce::Font("Segoe UI", 17.0f, juce::Font::bold));
        g.drawText(letter, 0, 0, getWidth(), getHeight(), juce::Justification::centred);

        g.setColour(Colors::textDim());
        g.setFont(juce::Font("Segoe UI", 9.0f, juce::Font::plain));
        g.drawText("+", getWidth() - 16, 2, 12, 12, juce::Justification::centred);
    }

    if (m_previewing)
    {
        g.setColour(Colors::success());
        g.drawRoundedRectangle(b.expanded(1.5f), radius + 1.0f, 2.0f);
    }

    if (m_selected)
    {
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.drawRoundedRectangle(b.reduced(1.5f), radius - 1.5f, 1.2f);
    }
}

void CuePadComponent::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onContextMenu) onContextMenu(m_index);
        return;
    }
    m_pressed = true;
    repaint();
    if (onPress) onPress(m_index);
}

void CuePadComponent::mouseUp(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    const bool wasPressed = m_pressed;
    m_pressed = false;
    repaint();
    if (wasPressed && onRelease) onRelease(m_index);
}

void CuePadComponent::mouseDoubleClick(const juce::MouseEvent&)
{
}

void CuePadComponent::mouseEnter(const juce::MouseEvent&)
{
    m_hovered = true;
    repaint();
}

void CuePadComponent::mouseExit(const juce::MouseEvent&)
{
    m_hovered = false;
    m_pressed = false;
    repaint();
}

} // namespace BeatMate::UI
