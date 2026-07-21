#include "MiniWaveformInline.h"
#include "../../styles/ColorPalette.h"

namespace BeatMate::UI {

MiniWaveformInline::MiniWaveformInline()
{
    setOpaque(false);
}

void MiniWaveformInline::setWaveformData(const std::vector<float>& low,
                                          const std::vector<float>& mid,
                                          const std::vector<float>& high)
{
    lowData_ = low;
    midData_ = mid;
    highData_ = high;
    repaint();
}

void MiniWaveformInline::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    float w = bounds.getWidth();
    float h = bounds.getHeight();
    float centreY = h * 0.5f;

    {
        juce::ColourGradient bgGrad(
            juce::Colour(0xFF141420), 0.0f, 0.0f,
            juce::Colour(0xFF0A0A12), 0.0f, h, false);
        g.setGradientFill(bgGrad);
        g.fillRect(bounds);
    }

    if (lowData_.empty() && midData_.empty() && highData_.empty())
    {
        g.setColour(juce::Colour(0x10FFFFFF));
        g.fillRect(bounds.getX(), centreY - 0.5f, w, 1.0f);
        return;
    }

    auto drawBand = [&](const std::vector<float>& data, juce::Colour colour, float alpha)
    {
        if (data.empty()) return;

        g.setColour(colour.withAlpha(alpha));
        size_t numSamples = data.size();
        float xScale = w / static_cast<float>(numSamples);

        for (size_t i = 0; i < numSamples; ++i)
        {
            float val = juce::jlimit(0.0f, 1.0f, data[i]);
            float barHeight = val * centreY;
            float x = static_cast<float>(i) * xScale;
            float barW = juce::jmax(1.0f, xScale - 0.5f);

            g.fillRect(x, centreY - barHeight, barW, barHeight * 2.0f);
        }
    };

    drawBand(lowData_,  Colors::waveformBass(),   0.6f);
    drawBand(midData_,  Colors::waveformMid(),    0.5f);
    drawBand(highData_, Colors::waveformTreble(), 0.4f);

    {
        g.setColour(juce::Colour(0x08FFFFFF));
        g.fillRect(bounds.getX(), centreY - 1.5f, w, 3.0f);
        g.setColour(juce::Colour(0x14FFFFFF));
        g.fillRect(bounds.getX(), centreY - 0.75f, w, 1.5f);
        g.setColour(juce::Colour(0x28FFFFFF));
        g.fillRect(bounds.getX(), centreY - 0.25f, w, 0.5f);
    }

    g.setColour(juce::Colour(0xFF1E1E2A));
    g.drawRect(bounds, 0.75f);
    g.setColour(Colors::glassBorder());
    g.drawRect(bounds.reduced(0.75f), 0.5f);
}

} // namespace BeatMate::UI
