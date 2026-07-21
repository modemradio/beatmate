#include "AnimatedProgressBar.h"
#include "../../styles/ColorPalette.h"
#include <cmath>

namespace BeatMate::UI {

AnimatedProgressBar::AnimatedProgressBar()
{
    startTimerHz(30);
}

AnimatedProgressBar::~AnimatedProgressBar()
{
    stopTimer();
}

void AnimatedProgressBar::setProgress(float progress)
{
    progress_ = juce::jlimit(0.0f, 1.0f, progress);
    repaint();
}

void AnimatedProgressBar::timerCallback()
{
    animationOffset_ += 0.02f;
    if (animationOffset_ > 2.0f)
        animationOffset_ -= 2.0f;
    repaint();
}

void AnimatedProgressBar::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    float cornerRadius = 6.0f;

    g.setColour(Colors::bgDark());
    g.fillRoundedRectangle(bounds, cornerRadius);

    if (progress_ > 0.0f)
    {
        auto fillBounds = bounds.withWidth(bounds.getWidth() * progress_);

        float gradStart = -bounds.getWidth() * animationOffset_;
        float gradEnd = gradStart + bounds.getWidth();

        juce::ColourGradient gradient(
            juce::Colour(0xFF0078D4), gradStart, bounds.getCentreY(),
            juce::Colour(0xFF8800FF), gradEnd, bounds.getCentreY(), false);
        gradient.addColour(0.5, juce::Colour(0xFF4A44FF));

        g.saveState();
        g.reduceClipRegion(fillBounds.toNearestInt());
        g.setGradientFill(gradient);
        g.fillRoundedRectangle(bounds, cornerRadius);
        g.restoreState();
    }

    g.setColour(juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.06f));
    g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);

    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.drawText(juce::String(static_cast<int>(progress_ * 100.0f)) + "%",
               bounds, juce::Justification::centred);
}

} // namespace BeatMate::UI
