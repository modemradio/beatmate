#include "CompatibilityBadge.h"
#include "../../styles/ColorPalette.h"

namespace BeatMate::UI {

CompatibilityBadge::CompatibilityBadge()
{
    setSize(80, 24);
    updateAppearance();
}

void CompatibilityBadge::setCompatibility(float score, const juce::String& details)
{
    score_ = juce::jlimit(0.0f, 1.0f, score);
    details_ = details;
    updateAppearance();
    repaint();
}

void CompatibilityBadge::updateAppearance()
{
    if (score_ > 0.8f)
    {
        label_ = "Perfect";
        badgeColour_ = Colors::success();
    }
    else if (score_ >= 0.5f)
    {
        label_ = "Good";
        badgeColour_ = Colors::warning();
    }
    else
    {
        label_ = "Clash";
        badgeColour_ = Colors::error();
    }
}

void CompatibilityBadge::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(badgeColour_.withAlpha(0.15f));
    g.fillRoundedRectangle(bounds, bounds.getHeight() * 0.5f);

    g.setColour(badgeColour_.withAlpha(0.4f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), bounds.getHeight() * 0.5f, 1.0f);

    g.setColour(badgeColour_);
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.drawText(label_, bounds, juce::Justification::centred);

    if (details_.isNotEmpty())
        juce::SettableTooltipClient::setTooltip(details_);
}

} // namespace BeatMate::UI
