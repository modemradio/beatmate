#include "GlassmorphicCard.h"
#include "../../styles/ColorPalette.h"

namespace BeatMate::UI {

GlassmorphicCard::GlassmorphicCard()
{
    setOpaque(false);
}

void GlassmorphicCard::setTitle(const juce::String& title)
{
    title_ = title;
    repaint();
}

void GlassmorphicCard::addContent(juce::Component* content)
{
    if (content != nullptr)
    {
        contentComponents_.push_back(content);
        addAndMakeVisible(content);
        resized();
    }
}

void GlassmorphicCard::setCornerRadius(float radius)
{
    cornerRadius_ = radius;
    repaint();
}

void GlassmorphicCard::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.03f));
    g.fillRoundedRectangle(bounds, cornerRadius_);

    g.setColour(juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.06f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), cornerRadius_, 1.0f);

    g.setColour(juce::Colour(0xFF0A0A12).withAlpha(0.4f));
    g.drawLine(bounds.getX() + 12, bounds.getBottom() - 0.5f,
               bounds.getRight() - 12, bounds.getBottom() - 0.5f, 1.0f);

    if (title_.isNotEmpty())
    {
        auto headerArea = bounds.removeFromTop(static_cast<float>(headerHeight_));

        g.setColour(juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.04f));
        g.fillRoundedRectangle(headerArea, cornerRadius_);

        g.setColour(juce::Colour(0xFF64748B));
        g.setFont(juce::Font("Segoe UI", 11.0f, juce::Font::bold));
        g.drawText(title_.toUpperCase(), headerArea.reduced(16.0f, 0.0f),
                   juce::Justification::centredLeft);
    }
}

void GlassmorphicCard::resized()
{
    auto area = getLocalBounds();

    if (title_.isNotEmpty())
        area.removeFromTop(headerHeight_);

    area.reduce(8, 8);

    int yOffset = area.getY();
    for (auto* comp : contentComponents_)
    {
        int h = comp->getHeight() > 0 ? comp->getHeight() : (area.getHeight() / juce::jmax(1, static_cast<int>(contentComponents_.size())));
        comp->setBounds(area.getX(), yOffset, area.getWidth(), h);
        yOffset += h + 4;
    }
}

} // namespace BeatMate::UI
