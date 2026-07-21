#include "GradientButton.h"
#include "../../styles/ColorPalette.h"

namespace BeatMate::UI {

GradientButton::GradientButton(const juce::String& text)
    : text_(text)
{
    setWantsKeyboardFocus(true);
}

void GradientButton::setText(const juce::String& text)
{
    text_ = text;
    repaint();
}

void GradientButton::setEnabled(bool enabled)
{
    enabled_ = enabled;
    repaint();
}

void GradientButton::focusGained(FocusChangeType)
{
    repaint();
}

void GradientButton::focusLost(FocusChangeType)
{
    repaint();
}

void GradientButton::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    float cornerRadius = 8.0f;

    if (!enabled_)
    {
        juce::Colour disabledStart = juce::Colour(0xFF0078D4).withSaturation(0.15f).withBrightness(0.25f);
        juce::Colour disabledEnd   = juce::Colour(0xFF8800FF).withSaturation(0.15f).withBrightness(0.25f);
        juce::ColourGradient disabledGrad(disabledStart, bounds.getX(), bounds.getY(),
                                           disabledEnd, bounds.getRight(), bounds.getBottom(), false);
        g.setGradientFill(disabledGrad);
        g.fillRoundedRectangle(bounds, cornerRadius);

        g.setColour(Colors::border().withAlpha(0.4f));
        g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);

        g.setColour(Colors::textMuted());
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.drawText(text_, bounds, juce::Justification::centred);
        return;
    }

    juce::Colour colStart = juce::Colour(0xFF0078D4);
    juce::Colour colEnd   = juce::Colour(0xFF8800FF);

    if (hovered_ || pressed_)
    {
        if (pressed_)
        {
            g.setColour(colStart.withAlpha(0.25f));
            g.fillRoundedRectangle(bounds.expanded(3.0f), cornerRadius + 3.0f);

            juce::ColourGradient gradient(colStart.brighter(0.25f), bounds.getX(), bounds.getY(),
                                           colEnd.brighter(0.25f), bounds.getRight(), bounds.getBottom(), false);
            g.setGradientFill(gradient);
            g.fillRoundedRectangle(bounds, cornerRadius);
        }
        else
        {
            juce::ColourGradient gradient(colStart, bounds.getX(), bounds.getY(),
                                           colEnd, bounds.getRight(), bounds.getBottom(), false);
            g.setGradientFill(gradient);
            g.fillRoundedRectangle(bounds, cornerRadius);
        }

        juce::ColourGradient topSheen(juce::Colours::white.withAlpha(0.12f), bounds.getCentreX(), bounds.getY(),
                                       juce::Colours::transparentWhite, bounds.getCentreX(), bounds.getY() + bounds.getHeight() * 0.4f, false);
        g.setGradientFill(topSheen);
        g.fillRoundedRectangle(bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight() * 0.5f, cornerRadius);
    }
    else
    {
        g.setColour(Colors::bgLighter());
        g.fillRoundedRectangle(bounds, cornerRadius);
        g.setColour(Colors::borderLight());
        g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);

        juce::ColourGradient topHL(juce::Colours::white.withAlpha(0.04f), bounds.getCentreX(), bounds.getY(),
                                    juce::Colours::transparentWhite, bounds.getCentreX(), bounds.getY() + bounds.getHeight() * 0.5f, false);
        g.setGradientFill(topHL);
        g.fillRoundedRectangle(bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight() * 0.5f, cornerRadius);
    }

    if (hasKeyboardFocus(true))
    {
        g.setColour(Colors::borderFocus().withAlpha(0.7f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), cornerRadius - 0.5f, 2.0f);
    }

    if (hovered_ || pressed_)
    {
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.drawText(text_, bounds.translated(0.0f, 1.0f), juce::Justification::centred);
    }

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(13.0f, juce::Font::bold));
    g.drawText(text_, bounds, juce::Justification::centred);
}

void GradientButton::mouseEnter(const juce::MouseEvent&)
{
    hovered_ = true;
    repaint();
}

void GradientButton::mouseExit(const juce::MouseEvent&)
{
    hovered_ = false;
    pressed_ = false;
    repaint();
}

void GradientButton::mouseDown(const juce::MouseEvent&)
{
    pressed_ = true;
    repaint();
}

void GradientButton::mouseUp(const juce::MouseEvent& e)
{
    if (pressed_ && enabled_ && contains(e.getPosition()))
    {
        if (onClick)
            onClick();
    }
    pressed_ = false;
    repaint();
}

}
