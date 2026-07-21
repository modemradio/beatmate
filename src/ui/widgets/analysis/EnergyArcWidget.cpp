#include "EnergyArcWidget.h"
#include "../../styles/ColorPalette.h"
#include <cmath>

namespace BeatMate::UI {

EnergyArcWidget::EnergyArcWidget()
{
    setSize(120, 120);
}

void EnergyArcWidget::setEnergy(float energy)
{
    energy_ = juce::jlimit(0.0f, 10.0f, energy);
    repaint();
}

juce::Colour EnergyArcWidget::getEnergyColour(float normalisedValue) const
{
    if (normalisedValue < 0.5f)
    {
        float t = normalisedValue * 2.0f;
        return Colors::success().interpolatedWith(Colors::warning(), t);
    }
    else
    {
        float t = (normalisedValue - 0.5f) * 2.0f;
        return Colors::warning().interpolatedWith(Colors::error(), t);
    }
}

void EnergyArcWidget::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(8.0f);
    float size = juce::jmin(bounds.getWidth(), bounds.getHeight());
    auto centre = bounds.getCentre();

    float radius = size * 0.45f;
    float arcThickness = 6.0f;

    float startAngle = juce::MathConstants<float>::pi * 0.75f;   // 135 degrees
    float endAngle = juce::MathConstants<float>::pi * 2.25f;     // 405 degrees
    float fullSweep = endAngle - startAngle;

    juce::Path bgArc;
    bgArc.addCentredArc(centre.x, centre.y, radius, radius,
                         0.0f, startAngle, endAngle, true);
    g.setColour(Colors::bgLightest().withAlpha(0.3f));
    g.strokePath(bgArc, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    float normalised = energy_ / 10.0f;
    if (normalised > 0.0f)
    {
        float valueAngle = startAngle + fullSweep * normalised;

        int segments = juce::jmax(1, static_cast<int>(normalised * 30.0f));
        float segmentAngle = (valueAngle - startAngle) / static_cast<float>(segments);

        for (int i = 0; i < segments; ++i)
        {
            float t = static_cast<float>(i) / static_cast<float>(juce::jmax(1, segments - 1));
            float a1 = startAngle + segmentAngle * static_cast<float>(i);
            float a2 = a1 + segmentAngle + 0.02f; // slight overlap

            juce::Path segPath;
            segPath.addCentredArc(centre.x, centre.y, radius, radius,
                                   0.0f, a1, a2, true);

            g.setColour(getEnergyColour(t * normalised));
            g.strokePath(segPath, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
        }
    }

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(28.0f, juce::Font::bold));
    g.drawText(juce::String(energy_, 1),
               bounds.reduced(radius * 0.3f), juce::Justification::centred);

    g.setColour(Colors::textMuted());
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawText("ENERGY",
               juce::Rectangle<float>(centre.x - 30.0f, centre.y + 12.0f, 60.0f, 16.0f),
               juce::Justification::centred);
}

} // namespace BeatMate::UI
