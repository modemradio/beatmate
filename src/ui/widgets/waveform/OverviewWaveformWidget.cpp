#include "OverviewWaveformWidget.h"
#include "../../styles/ColorPalette.h"
#include <cmath>
namespace BeatMate::UI {
OverviewWaveformWidget::OverviewWaveformWidget(){setOpaque(true);}
void OverviewWaveformWidget::setWaveformData(const std::vector<float>& peaks){m_peaks=peaks;repaint();}
void OverviewWaveformWidget::setVisibleRange(double start,double end){m_visibleStart=start;m_visibleEnd=end;repaint();}
void OverviewWaveformWidget::setPlayheadPosition(double pos){m_playheadPos=pos;repaint();}

void OverviewWaveformWidget::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    auto boundsF = bounds.toFloat();
    int w = bounds.getWidth(), h = bounds.getHeight();
    float wf = boundsF.getWidth(), hf = boundsF.getHeight();
    float cy = hf * 0.5f;

    {
        juce::ColourGradient bgGrad(
            juce::Colour(0xFF0D0D12), 0.0f, 0.0f,
            juce::Colour(0xFF080810), 0.0f, hf, false);
        g.setGradientFill(bgGrad);
        g.fillRect(boundsF);
    }

    if (m_peaks.empty())
    {
        g.setColour(Colors::border().withAlpha(0.8f));
        g.fillRect(6.0f, cy - 0.5f, wf - 12.0f, 1.0f);
        g.setColour(juce::Colour(0xFF1A1A24));
        g.drawRoundedRectangle(boundsF.reduced(0.5f), 3.0f, 1.0f);
        return;
    }

    juce::Path topPath, botPath;
    topPath.startNewSubPath(0.0f, cy);
    botPath.startNewSubPath(0.0f, cy);

    int numPeaks = static_cast<int>(m_peaks.size());
    for (int x = 0; x < w; ++x)
    {
        int idx = juce::jlimit(0, numPeaks - 1, (x * numPeaks) / w);
        float peak = std::abs(m_peaks[static_cast<size_t>(idx)]);
        float pH = peak * cy * 0.88f;
        float xf = static_cast<float>(x);
        topPath.lineTo(xf, cy - pH);
        botPath.lineTo(xf, cy + pH);
    }

    topPath.lineTo(wf, cy);
    topPath.closeSubPath();
    botPath.lineTo(wf, cy);
    botPath.closeSubPath();

    {
        juce::ColourGradient grad(
            juce::Colour(0xBB06B6D4), 0.0f, 0.0f,
            juce::Colour(0x33064060), 0.0f, cy, false);
        g.setGradientFill(grad);
        g.fillPath(topPath);
    }
    {
        juce::ColourGradient grad(
            juce::Colour(0x33064060), 0.0f, cy,
            juce::Colour(0xBB06B6D4), 0.0f, hf, false);
        g.setGradientFill(grad);
        g.fillPath(botPath);
    }

    g.setColour(juce::Colour(0x4067E8F9));
    g.strokePath(topPath, juce::PathStrokeType(0.5f));
    g.strokePath(botPath, juce::PathStrokeType(0.5f));

    g.setColour(juce::Colour(0x18FFFFFF));
    g.fillRect(0.0f, cy - 0.5f, wf, 1.0f);

    if (m_visibleEnd - m_visibleStart < 0.999)
    {
        float x1 = static_cast<float>(m_visibleStart * w);
        float x2 = static_cast<float>(m_visibleEnd * w);
        g.setColour(juce::Colour(0x14FFFFFF));
        g.fillRect(x1, 0.0f, x2 - x1, hf);
        g.setColour(Colors::primary().withAlpha(0.5f));
        g.drawRect(x1, 0.0f, x2 - x1, hf, 1.0f);
    }

    float px = static_cast<float>(m_playheadPos * w);
    g.setColour(juce::Colour(0x30FFFFFF));
    g.fillRect(px - 1.5f, 0.0f, 3.0f, hf);
    g.setColour(juce::Colours::white);
    g.fillRect(px - 0.5f, 0.0f, 1.0f, hf);

    g.setColour(juce::Colour(0xFF1A1A24));
    g.drawRoundedRectangle(boundsF.reduced(0.5f), 3.0f, 1.0f);
}

void OverviewWaveformWidget::mouseDown(const juce::MouseEvent& e){double pos=(double)e.x/getWidth();m_playheadPos=pos;m_listeners.call([pos](Listener&l){l.positionClicked(pos);});repaint();}
} // namespace BeatMate::UI
