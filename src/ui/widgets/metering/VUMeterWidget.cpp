#include "VUMeterWidget.h"
#include "../../styles/ColorPalette.h"

namespace BeatMate::UI {

VUMeterWidget::VUMeterWidget() { startTimer(30); }

void VUMeterWidget::setLevel(float level)
{
    m_level = juce::jlimit(0.0f, 1.0f, level);
    if (level > m_peakDecay)
    {
        m_peakDecay = level;
        m_peakLevel = level;
    }
    repaint();
}

void VUMeterWidget::setPeakHold(bool enabled) { m_peakHold = enabled; }

void VUMeterWidget::timerCallback()
{
    if (m_peakDecay > 0)
    {
        m_peakDecay -= 0.005f;
        if (m_peakDecay < m_level)
            m_peakDecay = m_level;
    }
    if (m_level > 0)
    {
        m_level -= 0.02f;
        if (m_level < 0) m_level = 0;
    }
    m_clipping = m_peakLevel > 0.95f;
    repaint();
}

void VUMeterWidget::paint(juce::Graphics& g)
{
    float w = (float)getWidth();
    float h = (float)getHeight();

    g.fillAll(Colors::bgDarkest());

    float scaleW = w > 30 ? 18.0f : 0.0f;
    float meterX = scaleW;
    float meterW = w - scaleW;
    float meterTop = 14.0f;     // leave room for clipping LED
    float meterBot = h - 4.0f;
    float meterH = meterBot - meterTop;

    if (scaleW > 0)
    {
        g.setFont(juce::Font(7.5f));
        g.setColour(Colors::textMuted());

        // dB values mapped to normalized positions: 0dB=1.0, -6dB~0.5, -12dB~0.25, etc.
        struct DbMark { float db; float norm; };
        const DbMark marks[] = {
            {  0.0f, 1.00f },
            { -3.0f, 0.85f },
            { -6.0f, 0.70f },
            { -9.0f, 0.55f },
            {-12.0f, 0.40f },
            {-18.0f, 0.22f },
            {-24.0f, 0.10f },
            {-36.0f, 0.02f }
        };
        for (auto& m : marks)
        {
            float y = meterBot - m.norm * meterH;
            juce::String label = (m.db == 0.0f) ? "0" : juce::String((int)m.db);
            g.drawText(label, 0, (int)(y - 5), (int)(scaleW - 2), 10, juce::Justification::centredRight);
            g.setColour(Colors::textDim());
            g.drawHorizontalLine((int)y, scaleW - 1, scaleW + 2);
            g.setColour(Colors::textMuted());
        }
    }

    float barH = m_level * meterH;
    {
        juce::ColourGradient grad(
            Colors::vuGreen(), 0, meterBot,
            Colors::vuRed(), 0, meterTop, false);
        grad.addColour(0.55, Colors::vuYellow());
        grad.addColour(0.80, juce::Colour(0xFFFF8800));

        g.setGradientFill(grad);
        g.fillRect(meterX + 2, meterBot - barH, meterW - 4, barH);
    }

    {
        g.setColour(Colors::bgDarkest().withAlpha(0.35f));
        int segments = (int)(meterH / 3.0f);
        for (int i = 0; i < segments; ++i)
        {
            float y = meterTop + i * 3.0f;
            g.drawHorizontalLine((int)y, meterX + 2, meterX + meterW - 2);
        }
    }

    {
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        for (int i = 0; i <= 10; ++i)
        {
            float y = meterTop + meterH * i / 10.0f;
            g.drawHorizontalLine((int)y, meterX, meterX + meterW);
        }
    }

    if (m_peakHold && m_peakDecay > 0.01f)
    {
        float peakY = meterBot - m_peakDecay * meterH;

        juce::Colour peakCol = juce::Colours::white;
        if (m_peakDecay > 0.85f) peakCol = Colors::vuRed();
        else if (m_peakDecay > 0.55f) peakCol = Colors::vuYellow();

        g.setColour(peakCol.withAlpha(0.25f));
        g.fillRect(meterX + 1, peakY - 2.0f, meterW - 2, 5.0f);

        g.setColour(peakCol.withAlpha(0.9f));
        g.fillRect(meterX + 2, peakY - 0.5f, meterW - 4, 1.5f);
    }

    {
        float ledR = 4.5f;
        float ledCX = meterX + meterW * 0.5f;
        float ledCY = 7.0f;

        if (m_clipping)
        {
            juce::ColourGradient clipGlow(
                Colors::vuRed().withAlpha(0.5f), ledCX, ledCY,
                Colors::vuRed().withAlpha(0.0f), ledCX + 12.0f, ledCY, true);
            g.setGradientFill(clipGlow);
            g.fillEllipse(ledCX - 12.0f, ledCY - 12.0f, 24.0f, 24.0f);

            g.setColour(Colors::vuRed());
        }
        else
        {
            g.setColour(Colors::vuRed().darker(0.8f));
        }
        g.fillEllipse(ledCX - ledR, ledCY - ledR, ledR * 2.0f, ledR * 2.0f);

        if (m_clipping)
        {
            juce::ColourGradient spec(
                juce::Colours::white.withAlpha(0.4f), ledCX - 1, ledCY - 1,
                juce::Colours::transparentWhite, ledCX + 2, ledCY + 2, true);
            g.setGradientFill(spec);
            g.fillEllipse(ledCX - ledR * 0.6f, ledCY - ledR * 0.6f, ledR * 1.2f, ledR * 1.2f);
        }
    }

    g.setColour(Colors::border());
    g.drawRect(meterX, meterTop, meterW, meterH + 1, 1.0f);
}

} // namespace BeatMate::UI
