#include <algorithm>
#include <cmath>
#include "ProWaveformWidget.h"
#include "../../styles/ColorPalette.h"

namespace BeatMate::UI {

ProWaveformWidget::ProWaveformWidget()
{
    setOpaque(true);
    startTimerHz(60);
    m_peaks.resize(4000);
    juce::Random rng;
    for (int i = 0; i < 4000; ++i)
        m_peaks[i] = static_cast<float>(std::sin(i * 0.03) * 0.6 + rng.nextFloat() * 0.4);
}

ProWaveformWidget::~ProWaveformWidget() { stopTimer(); }

void ProWaveformWidget::setWaveformData(const std::vector<float>& peaks) { m_peaks = peaks; repaint(); }
void ProWaveformWidget::setPlayheadPosition(double pos) { m_playheadPos = pos; }
void ProWaveformWidget::setZoom(double zoom) { m_zoom = juce::jlimit(1.0, 100.0, zoom); }

void ProWaveformWidget::paint(juce::Graphics& g)
{
    auto boundsF = getLocalBounds().toFloat();
    int w = getWidth(), h = getHeight();
    float wf = boundsF.getWidth(), hf = boundsF.getHeight();
    float cy = hf * 0.5f;

    {
        juce::ColourGradient bgGrad(
            juce::Colour(0xFF0D0D12), 0.0f, 0.0f,
            juce::Colour(0xFF060609), 0.0f, hf, false);
        g.setGradientFill(bgGrad);
        g.fillRect(boundsF);
    }

    {
        float vigH = 14.0f;
        juce::ColourGradient topVig(
            juce::Colour(0x1C000000), 0.0f, 0.0f,
            juce::Colour(0x00000000), 0.0f, vigH, false);
        g.setGradientFill(topVig);
        g.fillRect(0.0f, 0.0f, wf, vigH);

        juce::ColourGradient botVig(
            juce::Colour(0x00000000), 0.0f, hf - vigH,
            juce::Colour(0x1C000000), 0.0f, hf, false);
        g.setGradientFill(botVig);
        g.fillRect(0.0f, hf - vigH, wf, vigH);
    }

    if (m_peaks.empty()) return;

    double vs = m_scrollOffset;
    double ve = std::min(1.0, m_scrollOffset + 1.0 / m_zoom);
    int ss = static_cast<int>(vs * m_peaks.size());
    int es = static_cast<int>(ve * m_peaks.size());
    int rng = es - ss;
    if (rng <= 0) return;

    float maxH = cy * 0.85f;

    for (int x = 0; x < w; ++x)
    {
        int idx = juce::jlimit(0, static_cast<int>(m_peaks.size()) - 1, ss + (x * rng) / w);
        float peak = std::abs(m_peaks[idx]);
        float intensity = peak;
        float peakH = peak * maxH;

        if (peakH < 0.5f) continue;

        float xf = static_cast<float>(x);

        juce::Colour barCol = juce::Colour::fromFloatRGBA(
            0.0f, 0.85f * intensity, 1.0f * intensity, 0.85f);

        {
            juce::ColourGradient grad(
                barCol, xf, cy - peakH,
                barCol.withAlpha(0.06f), xf, cy, false);
            g.setGradientFill(grad);
            g.fillRect(xf, cy - peakH, 1.0f, peakH);
        }
        {
            juce::ColourGradient grad(
                barCol.withAlpha(0.06f), xf, cy,
                barCol, xf, cy + peakH, false);
            g.setGradientFill(grad);
            g.fillRect(xf, cy, 1.0f, peakH);
        }

        if (peakH > 3.0f)
        {
            juce::Colour brightTip = barCol.brighter(0.7f).withAlpha(0.92f);
            g.setColour(brightTip);
            g.fillRect(xf, cy - peakH, 1.0f, 1.0f);
            g.fillRect(xf, cy + peakH - 1.0f, 1.0f, 1.0f);
        }

        if (peak > 0.7f)
        {
            float glowAlpha = (peak - 0.7f) * 0.35f;
            g.setColour(barCol.withAlpha(glowAlpha));
            g.fillRect(xf - 1.0f, cy - peakH - 2.0f, 3.0f, peakH * 2.0f + 4.0f);
        }
    }

    {
        g.setColour(juce::Colour(0x0AFFFFFF));
        g.fillRect(0.0f, cy - 2.0f, wf, 4.0f);
        g.setColour(juce::Colour(0x18FFFFFF));
        g.fillRect(0.0f, cy - 1.0f, wf, 2.0f);
        g.setColour(juce::Colour(0x30FFFFFF));
        g.fillRect(0.0f, cy - 0.5f, wf, 1.0f);
    }

    if (m_playheadPos >= vs && m_playheadPos <= ve)
    {
        float px = static_cast<float>((m_playheadPos - vs) / (ve - vs) * w);

        {
            juce::ColourGradient glowL(
                juce::Colour(0x00FFFFFF), px - 18.0f, 0.0f,
                juce::Colour(0x12FFFFFF), px, 0.0f, false);
            g.setGradientFill(glowL);
            g.fillRect(px - 18.0f, 0.0f, 18.0f, hf);

            juce::ColourGradient glowR(
                juce::Colour(0x12FFFFFF), px, 0.0f,
                juce::Colour(0x00FFFFFF), px + 18.0f, 0.0f, false);
            g.setGradientFill(glowR);
            g.fillRect(px, 0.0f, 18.0f, hf);
        }

        g.setColour(juce::Colour(0x38FFFFFF));
        g.fillRect(px - 2.0f, 0.0f, 4.0f, hf);

        g.setColour(juce::Colours::white.withAlpha(0.95f));
        g.fillRect(px - 0.75f, 0.0f, 1.5f, hf);

        {
            juce::Path tri;
            tri.addTriangle(px - 6.0f, 0.0f, px + 6.0f, 0.0f, px, 7.0f);
            g.setColour(juce::Colours::white);
            g.fillPath(tri);
        }
        {
            juce::Path tri;
            tri.addTriangle(px - 6.0f, hf, px + 6.0f, hf, px, hf - 7.0f);
            g.setColour(juce::Colours::white);
            g.fillPath(tri);
        }
    }

    g.setColour(juce::Colour(0xFF1A1A24));
    g.drawRoundedRectangle(boundsF.reduced(0.5f), 6.0f, 1.5f);
    g.setColour(juce::Colour(0xFF2A2A3A));
    g.drawRoundedRectangle(boundsF.reduced(1.5f), 5.0f, 0.5f);
}

void ProWaveformWidget::mouseDown(const juce::MouseEvent& e)
{
    double vs = m_scrollOffset, ve = m_scrollOffset + 1.0 / m_zoom;
    double pos = vs + (static_cast<double>(e.x) / getWidth()) * (ve - vs);
    m_playheadPos = pos;
    m_listeners.call([pos](Listener& l) { l.positionClicked(pos); });
}

void ProWaveformWidget::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    setZoom(m_zoom * (w.deltaY > 0 ? 1.15 : 1.0 / 1.15));
}

} // namespace BeatMate::UI
