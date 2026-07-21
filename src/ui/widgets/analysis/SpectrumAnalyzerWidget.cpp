#include <algorithm>
#include <cmath>
#include "SpectrumAnalyzerWidget.h"
#include "../../styles/ColorPalette.h"

namespace BeatMate::UI {

SpectrumAnalyzerWidget::SpectrumAnalyzerWidget()
{
    m_bands.resize(m_barCount, 0.0f);
    m_displayBands.resize(m_barCount, 0.0f);
    m_peakHold.resize(m_barCount, 0.0f);
    juce::Random r;
    for (int i = 0; i < m_barCount; ++i)
    {
        m_bands[i] = r.nextFloat();
        m_displayBands[i] = m_bands[i];
        m_peakHold[i] = m_bands[i];
    }
    startTimer(30);
}

void SpectrumAnalyzerWidget::setSpectrumData(const std::vector<float>& bands)
{
    m_bands = bands;
    m_displayBands.resize(m_bands.size(), 0.0f);
    m_peakHold.resize(m_bands.size(), 0.0f);
    for (size_t i = 0; i < m_bands.size(); ++i)
        if (m_bands[i] > m_peakHold[i])
            m_peakHold[i] = m_bands[i];
    repaint();
}

void SpectrumAnalyzerWidget::setBarCount(int count)
{
    m_barCount = count;
    m_bands.resize(count, 0.0f);
    m_displayBands.resize(count, 0.0f);
    m_peakHold.resize(count, 0.0f);
}

void SpectrumAnalyzerWidget::timerCallback()
{
    for (size_t i = 0; i < m_displayBands.size() && i < m_bands.size(); ++i)
    {
        float diff = m_bands[i] - m_displayBands[i];
        if (diff > 0)
            m_displayBands[i] += diff * 0.35f;
        else
            m_displayBands[i] += diff * 0.12f;
    }
    for (size_t i = 0; i < m_peakHold.size(); ++i)
        if (m_peakHold[i] > m_displayBands[i])
            m_peakHold[i] -= 0.008f;
    repaint();
}

void SpectrumAnalyzerWidget::paint(juce::Graphics& g)
{
    float w = (float)getWidth();
    float h = (float)getHeight();

    g.fillAll(Colors::bgDarkest());

    if (m_displayBands.empty()) return;

    int count = (int)m_displayBands.size();

    float labelH = 14.0f;
    float meterTop = 2.0f;
    float meterBot = h - labelH;
    float meterH = meterBot - meterTop;
    float barArea = w - 4.0f;
    float barW = barArea / count;
    float gap = barW > 5.0f ? 1.0f : 0.0f;

    g.setColour(juce::Colours::white.withAlpha(0.04f));
    for (int i = 1; i < 5; ++i)
    {
        float y = meterTop + meterH * i / 5.0f;
        g.drawHorizontalLine((int)y, 2.0f, w - 2.0f);
    }

    for (int i = 0; i < count; ++i)
    {
        float x = 2.0f + i * barW;
        float barH = m_displayBands[i] * meterH;
        if (barH < 0.5f) continue;

        float barTop = meterBot - barH;

        juce::ColourGradient barGrad(
            Colors::accent(), x, meterBot,
            Colors::secondary(), x, meterTop, false);
        barGrad.addColour(0.5, Colors::primary());
        g.setGradientFill(barGrad);
        g.fillRect(x, barTop, barW - gap, barH);

        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.fillRect(x, barTop, barW - gap, std::min(2.0f, barH));

        if (barW > 4.0f)
        {
            g.setColour(juce::Colours::white.withAlpha(0.06f));
            g.fillRect(x, barTop, (barW - gap) * 0.4f, barH);
        }
    }

    for (int i = 0; i < (int)m_peakHold.size() && i < count; ++i)
    {
        if (m_peakHold[i] < 0.01f) continue;
        float x = 2.0f + i * barW;
        float peakY = meterBot - m_peakHold[i] * meterH;

        juce::Colour peakCol = Colors::primaryHover();
        g.setColour(peakCol.withAlpha(0.2f));
        g.fillRect(x, peakY - 1.5f, barW - gap, 4.0f);

        g.setColour(peakCol.withAlpha(0.85f));
        g.fillRect(x, peakY - 0.5f, barW - gap, 1.5f);
    }

    g.setFont(juce::Font(7.5f));
    g.setColour(Colors::textMuted());

    auto barToFreq = [count](int idx) -> float {
        float t = (float)idx / (float)(count - 1);
        return 20.0f * std::pow(1000.0f, t);
    };

    auto formatFreq = [](float freq) -> juce::String {
        if (freq >= 1000.0f)
            return juce::String(freq / 1000.0f, 1) + "k";
        return juce::String((int)freq);
    };

    int labelCount = juce::jmin(7, count);
    for (int li = 0; li < labelCount; ++li)
    {
        int idx = (li * (count - 1)) / (labelCount - 1);
        float x = 2.0f + idx * barW;
        float freq = barToFreq(idx);
        juce::String label = formatFreq(freq);
        g.drawText(label, (int)(x - 12), (int)meterBot + 1, 26, (int)labelH,
                   juce::Justification::centred);
    }

    g.setColour(Colors::border());
    g.drawRect(0.0f, 0.0f, w, h, 1.0f);
}

} // namespace BeatMate::UI
