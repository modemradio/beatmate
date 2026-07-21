#include <algorithm>
#include <cmath>
#include "LoadingSpinner.h"
#include "../../styles/ColorPalette.h"

namespace BeatMate::UI {

LoadingSpinner::LoadingSpinner() { setSize(60, 60); setVisible(false); }

void LoadingSpinner::start() { m_active = true; setVisible(true); startTimer(30); }
void LoadingSpinner::stop() { m_active = false; stopTimer(); setVisible(false); }

void LoadingSpinner::timerCallback()
{
    m_angle = (m_angle + 8) % 360;
    repaint();
}

void LoadingSpinner::paint(juce::Graphics& g)
{
    if (m_overlay)
        g.fillAll(juce::Colour(static_cast<juce::uint8>(0), 0, 0, static_cast<juce::uint8>(120)));

    int side = std::min(getWidth(), getHeight());
    float cx = static_cast<float>(getWidth()) * 0.5f;
    float cy = static_cast<float>(getHeight()) * 0.5f;
    float radius = static_cast<float>(side) * 0.25f;
    float strokeW = 3.0f;

    float startAngle = juce::degreesToRadians(static_cast<float>(m_angle));
    float arcLength = juce::MathConstants<float>::pi * 1.5f;

    juce::Colour colStart(0xFF3B82F6);
    juce::Colour colEnd(0xFF8B5CF6);

    int glowSteps = 6;
    for (int i = glowSteps; i >= 1; --i)
    {
        float tailOffset = static_cast<float>(i) * 0.08f;
        float tailAlpha = 0.06f * (1.0f - static_cast<float>(i) / static_cast<float>(glowSteps + 1));
        float tailStroke = strokeW + static_cast<float>(i) * 1.5f;

        float tailStart = startAngle - tailOffset;

        juce::Path tailArc;
        tailArc.addCentredArc(cx, cy, radius, radius, tailStart, 0.0f, arcLength, true);

        g.setColour(colStart.withAlpha(tailAlpha));
        g.strokePath(tailArc, juce::PathStrokeType(tailStroke, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }

    int numSegments = 32;
    float segAngle = arcLength / static_cast<float>(numSegments);

    for (int i = 0; i < numSegments; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(numSegments - 1);
        float segStart = static_cast<float>(i) * segAngle;
        float segEnd = segStart + segAngle + 0.02f; // tiny overlap to avoid gaps

        juce::Colour segCol = colStart.interpolatedWith(colEnd, t);

        juce::Path seg;
        seg.addCentredArc(cx, cy, radius, radius, startAngle, segStart, segEnd, true);

        g.setColour(segCol);
        g.strokePath(seg, juce::PathStrokeType(strokeW, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    {
        float tipAngle = startAngle + arcLength;
        float dotX = cx + std::cos(tipAngle) * radius;
        float dotY = cy + std::sin(tipAngle) * radius;
        float dotR = strokeW * 0.65f;

        g.setColour(colEnd.withAlpha(0.25f));
        g.fillEllipse(dotX - dotR * 2.0f, dotY - dotR * 2.0f, dotR * 4.0f, dotR * 4.0f);

        g.setColour(colEnd.brighter(0.4f));
        g.fillEllipse(dotX - dotR, dotY - dotR, dotR * 2.0f, dotR * 2.0f);
    }
}

} // namespace BeatMate::UI
