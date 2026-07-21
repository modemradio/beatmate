#include "ProVUMeterWidget.h"
#include "../../styles/ColorPalette.h"
#include <cmath>
namespace BeatMate::UI {

static constexpr int   kTimerIntervalMs     = 30;
static constexpr float kFramesPerSecond     = 1000.0f / (float)kTimerIntervalMs;
static constexpr float kPeakHoldSeconds     = 1.7f;
static constexpr int   kPeakHoldFrames      = (int)(kPeakHoldSeconds * kFramesPerSecond + 0.5f);
static constexpr float kDecayDbPerSecond    = 20.0f / 1.7f;  // ≈ 11.76 dB/s
static constexpr float kDecayDbPerFrame     = kDecayDbPerSecond / kFramesPerSecond;
static constexpr float kDecayNormPerFrame   = kDecayDbPerFrame / 66.0f;

ProVUMeterWidget::ProVUMeterWidget() { startTimer(kTimerIntervalMs); }

void ProVUMeterWidget::setLevel(float level) {
    m_level = juce::jlimit(0.0f, 1.0f, level);
    if (level > m_peakDecay) {
        m_peakDecay = level;
        m_peakLevel = level;
        m_peakHoldCounter = 0; // restart the hold window on any new peak
    }
    m_clipping = m_peakLevel > 0.95f;
    repaint();
}

void ProVUMeterWidget::setLevelDb(float db) { setLevel(dbToNormalized(db)); }

float ProVUMeterWidget::dbToNormalized(float db) const {
    if (db <= -60.0f) return 0.0f;
    if (db >= 6.0f) return 1.0f;
    return (db + 60.0f) / 66.0f;
}

void ProVUMeterWidget::timerCallback() {
    m_smoothLevel += (m_level - m_smoothLevel) * 0.3f;
    if (m_level > 0.0f) {
        m_level -= kDecayNormPerFrame;
        if (m_level < 0.0f) m_level = 0.0f;
    }
    m_peakHoldCounter++;
    if (m_peakHoldEnabled && m_peakHoldCounter > kPeakHoldFrames) {
        m_peakDecay -= kDecayNormPerFrame;
        if (m_peakDecay < m_level) m_peakDecay = m_level;
        if (m_peakDecay < 0.0f) m_peakDecay = 0.0f;
    }
    repaint();
}

void ProVUMeterWidget::paint(juce::Graphics& g) {
    int w = getWidth(), h = getHeight();
    g.fillAll(Colors::bgDarkest());

    bool vert = (m_orientation == Vertical);
    int scaleWidth = m_showScale ? 18 : 0;
    int meterX = vert ? (w - m_meterWidth) / 2 : scaleWidth;
    int meterY = vert ? 10 : (h - m_meterWidth) / 2;
    int meterW = vert ? m_meterWidth : (w - scaleWidth - 4);
    int meterH = vert ? (h - 20) : m_meterWidth;

    g.setColour(juce::Colour(0xFF0D0D0D));
    g.fillRoundedRectangle((float)meterX, (float)meterY, (float)meterW, (float)meterH, 2.0f);

    float fillAmount = m_smoothLevel;
    if (vert) {
        int fillH = (int)(fillAmount * meterH);
        if (fillH > 0) {
            juce::ColourGradient grad(
                juce::Colour(0xFF00E040), (float)meterX, (float)(meterY + meterH),
                juce::Colour(0xFFFF1010), (float)meterX, (float)meterY, false);
            grad.addColour(0.35, juce::Colour(0xFF00FF50));
            grad.addColour(0.55, juce::Colour(0xFF80FF00));
            grad.addColour(0.65, juce::Colour(0xFFFFFF00));
            grad.addColour(0.75, juce::Colour(0xFFFFCC00));
            grad.addColour(0.85, juce::Colour(0xFFFF6600));
            grad.addColour(0.92, juce::Colour(0xFFFF3300));
            g.setGradientFill(grad);

            if (m_segmented) {
                int segH = 3, gap = 1;
                for (int y = meterY + meterH - fillH; y < meterY + meterH; y += segH + gap) {
                    int sh = std::min(segH, meterY + meterH - y);
                    g.fillRect(meterX + 1, y, meterW - 2, sh);
                }
            } else {
                g.fillRoundedRectangle((float)(meterX + 1), (float)(meterY + meterH - fillH),
                                       (float)(meterW - 2), (float)fillH, 1.0f);
            }
        }
    } else {
        int fillW = (int)(fillAmount * meterW);
        if (fillW > 0) {
            juce::ColourGradient grad(
                juce::Colour(0xFF00E040), (float)meterX, (float)meterY,
                juce::Colour(0xFFFF1010), (float)(meterX + meterW), (float)meterY, false);
            grad.addColour(0.55, juce::Colour(0xFFFFFF00));
            grad.addColour(0.85, juce::Colour(0xFFFF6600));
            g.setGradientFill(grad);
            g.fillRoundedRectangle((float)(meterX + 1), (float)(meterY + 1),
                                   (float)fillW, (float)(meterH - 2), 1.0f);
        }
    }

    g.setColour(juce::Colour((juce::uint8)255, 255, 255, (juce::uint8)20));
    if (vert) {
        for (int i = 1; i < 20; ++i) {
            int y = meterY + meterH * i / 20;
            g.drawHorizontalLine(y, (float)meterX, (float)(meterX + meterW));
        }
    }

    if (m_peakHoldEnabled && m_peakDecay > 0.01f) {
        g.setColour(juce::Colours::white);
        if (vert) {
            int peakY = meterY + meterH - (int)(m_peakDecay * meterH);
            g.fillRect(meterX + 1, peakY, meterW - 2, 2);
        } else {
            int peakX = meterX + (int)(m_peakDecay * meterW);
            g.fillRect(peakX, meterY + 1, 2, meterH - 2);
        }
    }

    if (vert) {
        float ledX = (float)(w / 2 - 4), ledY = 1.0f;
        g.setColour(m_clipping ? juce::Colour(0xFFFF0000) : juce::Colour(0xFF2A0000));
        g.fillRoundedRectangle(ledX, ledY, 8.0f, 6.0f, 2.0f);
    }

    if (m_showScale && vert) {
        g.setFont(juce::Font(7.0f));
        g.setColour(Colors::textDim());
        float dbValues[] = {0, -3, -6, -10, -20, -30, -40, -60};
        for (float db : dbValues) {
            float norm = dbToNormalized(db);
            int y = meterY + meterH - (int)(norm * meterH);
            g.drawText(juce::String((int)db), 0, y - 5, meterX - 2, 10, juce::Justification::right);
            g.setColour(juce::Colour((juce::uint8)255, 255, 255, (juce::uint8)30));
            g.drawHorizontalLine(y, (float)meterX, (float)(meterX + meterW));
            g.setColour(Colors::textDim());
        }
    }

    g.setColour(Colors::border());
    g.drawRoundedRectangle((float)meterX, (float)meterY, (float)meterW, (float)meterH, 2.0f, 1.0f);
}

} // namespace BeatMate::UI
