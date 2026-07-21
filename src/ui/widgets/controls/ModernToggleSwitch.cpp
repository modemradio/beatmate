#include "ModernToggleSwitch.h"
#include "../../styles/ColorPalette.h"

namespace BeatMate::UI {

ModernToggleSwitch::ModernToggleSwitch()
{
    setSize(48, 24);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus(true);
}

void ModernToggleSwitch::setOn(bool on)
{
    if (m_on == on) return;
    m_on = on;
    m_targetPosition = on ? 1.0 : 0.0;
    startTimerHz(60);
    m_listeners.call([on](Listener& l) { l.toggled(on); });
}

void ModernToggleSwitch::timerCallback()
{
    double diff = m_targetPosition - m_position;
    if (std::abs(diff) < 0.005)
    {
        m_position = m_targetPosition;
        stopTimer();
    }
    else
    {
        double speed = 0.18 + 0.12 * (1.0 - std::abs(diff));
        m_position += diff * speed;
    }
    repaint();
}

void ModernToggleSwitch::paint(juce::Graphics& g)
{
    float w = (float)getWidth();
    float h = (float)getHeight();
    float radius = h * 0.5f;
    float pos = (float)m_position;

    juce::Colour offTrack  = Colors::bgLightest();
    juce::Colour onTrack   = juce::Colour(0xFF0078D4);
    juce::Colour trackCol  = offTrack.interpolatedWith(onTrack, pos);

    g.setColour(trackCol);
    g.fillRoundedRectangle(0, 0, w, h, radius);

    juce::ColourGradient innerShadow(
        juce::Colours::black.withAlpha(0.15f), w * 0.5f, 0.0f,
        juce::Colours::transparentBlack, w * 0.5f, h * 0.35f, false);
    g.setGradientFill(innerShadow);
    g.fillRoundedRectangle(0, 0, w, h * 0.5f, radius);

    if (hasKeyboardFocus(true))
    {
        g.setColour(Colors::borderFocus().withAlpha(0.6f));
        g.drawRoundedRectangle(juce::Rectangle<float>(0, 0, w, h).reduced(0.5f), radius, 2.0f);
    }

    float thumbR = h * 0.5f - 3.0f;
    float thumbX = 3.0f + pos * (w - h);
    float thumbCY = h * 0.5f;

    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillEllipse(thumbX - 0.5f, thumbCY - thumbR + 1.5f, thumbR * 2.0f + 1.0f, thumbR * 2.0f + 1.0f);

    juce::ColourGradient thumbGrad(
        juce::Colours::white, thumbX + thumbR, thumbCY - thumbR * 0.6f,
        juce::Colour(0xFFDDDDDD), thumbX + thumbR, thumbCY + thumbR * 0.8f, false);
    g.setGradientFill(thumbGrad);
    g.fillEllipse(thumbX, thumbCY - thumbR, thumbR * 2.0f, thumbR * 2.0f);

    if (pos > 0.01f)
    {
        float glowR = thumbR * 2.2f;
        juce::ColourGradient glow(
            onTrack.withAlpha(0.35f * pos), thumbX + thumbR, thumbCY,
            onTrack.withAlpha(0.0f), thumbX + thumbR + glowR, thumbCY, true);
        g.setGradientFill(glow);
        g.fillEllipse(thumbX + thumbR - glowR, thumbCY - glowR, glowR * 2.0f, glowR * 2.0f);
    }

    juce::ColourGradient spec(
        juce::Colours::white.withAlpha(0.5f),
        thumbX + thumbR * 0.6f, thumbCY - thumbR * 0.4f,
        juce::Colours::transparentWhite,
        thumbX + thumbR * 1.2f, thumbCY + thumbR * 0.6f, true);
    g.setGradientFill(spec);
    g.fillEllipse(thumbX + thumbR * 0.3f, thumbCY - thumbR * 0.7f, thumbR * 1.0f, thumbR * 1.0f);

    g.setColour(juce::Colours::black.withAlpha(0.2f));
    g.drawRoundedRectangle(0.5f, 0.5f, w - 1.0f, h - 1.0f, radius, 0.5f);
}

void ModernToggleSwitch::mouseDown(const juce::MouseEvent&)
{
    setOn(!m_on);
}

} // namespace BeatMate::UI
