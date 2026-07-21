#include "LEDButton.h"
#include "../../styles/ColorPalette.h"

namespace BeatMate::UI {

LEDButton::LEDButton(const juce::String& text)
    : m_text(text)
{
    setSize(60, 30);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus(true);
}

void LEDButton::setActive(bool active)
{
    m_active = active;
    m_targetGlowAlpha = active ? 1.0f : 0.0f;
    startTimerHz(60);
    m_listeners.call([active](Listener& l) { l.toggled(active); });
    repaint();
}

void LEDButton::timerCallback()
{
    float diff = m_targetGlowAlpha - m_glowAlpha;
    if (std::abs(diff) < 0.01f)
    {
        m_glowAlpha = m_targetGlowAlpha;
        stopTimer();
    }
    else
    {
        m_glowAlpha += diff * 0.15f;
    }
    repaint();
}

void LEDButton::mouseEnter(const juce::MouseEvent&)
{
    m_hovered = true;
    repaint();
}

void LEDButton::mouseExit(const juce::MouseEvent&)
{
    m_hovered = false;
    repaint();
}

void LEDButton::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    float w = bounds.getWidth();
    float h = bounds.getHeight();
    float cornerRadius = 5.0f;

    juce::Colour bgBase = m_active ? Colors::bgElevated().brighter(0.05f) : Colors::bgCard();
    if (m_hovered)
        bgBase = bgBase.brighter(0.08f);

    g.setColour(bgBase);
    g.fillRoundedRectangle(bounds, cornerRadius);

    g.setColour(m_active ? m_ledColor.withAlpha(0.25f) : Colors::border());
    g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);

    if (hasKeyboardFocus(true))
    {
        g.setColour(Colors::borderFocus().withAlpha(0.6f));
        g.drawRoundedRectangle(bounds.reduced(1.0f), cornerRadius - 1.0f, 1.5f);
    }

    float ledR = m_ledSize * 0.5f;
    float ledCX = bounds.getX() + 5.0f + ledR;
    float ledCY = bounds.getCentreY();

    juce::Colour ledCol = m_ledColor.interpolatedWith(m_ledColor.darker(0.85f), 1.0f - m_glowAlpha);

    if (m_glowAlpha > 0.01f)
    {
        float glowR = ledR * 3.0f;
        juce::ColourGradient outerGlow(
            m_ledColor.withAlpha(0.30f * m_glowAlpha),
            ledCX, ledCY,
            m_ledColor.withAlpha(0.0f),
            ledCX + glowR, ledCY,
            true);
        g.setGradientFill(outerGlow);
        g.fillEllipse(ledCX - glowR, ledCY - glowR, glowR * 2.0f, glowR * 2.0f);
    }

    if (m_glowAlpha > 0.01f)
    {
        float innerGlowR = ledR * 1.8f;
        juce::ColourGradient innerGlow(
            m_ledColor.withAlpha(0.5f * m_glowAlpha),
            ledCX, ledCY,
            m_ledColor.withAlpha(0.0f),
            ledCX + innerGlowR, ledCY,
            true);
        g.setGradientFill(innerGlow);
        g.fillEllipse(ledCX - innerGlowR, ledCY - innerGlowR, innerGlowR * 2.0f, innerGlowR * 2.0f);
    }

    g.setColour(ledCol);
    g.fillEllipse(ledCX - ledR, ledCY - ledR, ledR * 2.0f, ledR * 2.0f);

    juce::ColourGradient spec(
        juce::Colours::white.withAlpha(0.35f * m_glowAlpha),
        ledCX - ledR * 0.3f, ledCY - ledR * 0.3f,
        juce::Colours::transparentWhite,
        ledCX + ledR * 0.5f, ledCY + ledR * 0.5f,
        true);
    g.setGradientFill(spec);
    g.fillEllipse(ledCX - ledR * 0.7f, ledCY - ledR * 0.7f, ledR * 1.4f, ledR * 1.4f);

    float textX = ledCX + ledR + 6.0f;
    g.setColour(m_active ? Colors::textPrimary() : Colors::textSecondary());
    g.setFont(juce::Font(10.0f));
    g.drawText(m_text, (int)textX, 0, (int)(w - textX - 2), (int)h,
               juce::Justification::centredLeft);
}

void LEDButton::mouseDown(const juce::MouseEvent&)
{
    setActive(!m_active);
    m_listeners.call([](Listener& l) { l.clicked(); });
}

}
