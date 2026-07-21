#include "PeakMeterDual.h"
#include "../../styles/ColorPalette.h"
#include <algorithm>
#include <cmath>

namespace BeatMate::UI {

static constexpr int   kTimerHz       = 30;
static constexpr float kTimerDt       = 1.0f / static_cast<float>(kTimerHz);
static constexpr float kBarDecayDbPerSec      = 20.0f;
static constexpr float kPeakDecayDbPerSec     = 60.0f;

namespace {

inline float dbToAmp(float db) noexcept
{
    return std::pow(10.0f, db * 0.05f);
}

inline float ampToDb(float amp) noexcept
{
    return 20.0f * std::log10(std::max(1.0e-7f, amp));
}

inline float decayAmpDb(float currentAmp, float dbPerSec) noexcept
{
    if (currentAmp <= 1.0e-6f) return 0.0f;
    const float currentDb = ampToDb(currentAmp);
    const float nextDb    = currentDb - dbPerSec * kTimerDt;
    if (nextDb <= -80.0f) return 0.0f;
    return dbToAmp(nextDb);
}

} // namespace

PeakMeterDual::PeakMeterDual()
{
    setOpaque(false);
    setInterceptsMouseClicks(false, false);
    startTimerHz(kTimerHz);
}

void PeakMeterDual::setLevels(float left, float right) noexcept
{
    m_targetL = juce::jlimit(0.0f, 1.5f, left);
    m_targetR = juce::jlimit(0.0f, 1.5f, right);
}

float PeakMeterDual::ampToNorm(float amp01) noexcept
{
    if (amp01 <= 1.0e-6f) return 0.0f;
    const float db = ampToDb(amp01);
    if (db <= kFloorDb) return 0.0f;
    if (db >= 0.0f)     return 1.0f;
    return (db - kFloorDb) / (-kFloorDb);
}

void PeakMeterDual::timerCallback()
{
    auto follow = [](float& display, float target) {
        if (target >= display) display = target;
        else                   display = std::max(target, decayAmpDb(display, kBarDecayDbPerSec));
    };
    follow(m_displayL, m_targetL);
    follow(m_displayR, m_targetR);

    auto holdFollow = [](float& peak, float bar) {
        if (bar >= peak) peak = bar;
        else             peak = std::max(bar, decayAmpDb(peak, kPeakDecayDbPerSec));
    };
    holdFollow(m_peakHoldL, m_displayL);
    holdFollow(m_peakHoldR, m_displayR);

    repaint();
}

void PeakMeterDual::drawChannel(juce::Graphics& g, juce::Rectangle<int> bar,
                                float displayAmp, float peakAmp) const
{
    g.setColour(juce::Colour(0xFF0B0B12));
    g.fillRect(bar);

    if (bar.getHeight() <= 0 || bar.getWidth() <= 0) return;

    const float norm = ampToNorm(displayAmp);
    const int   activeH = static_cast<int>(std::round(norm * static_cast<float>(bar.getHeight())));
    if (activeH > 0) {
        const float yRed    = bar.getY()      + (1.0f - ampToNorm(dbToAmp(  0.0f))) * bar.getHeight();
        const float yYellow = bar.getY()      + (1.0f - ampToNorm(dbToAmp( -3.0f))) * bar.getHeight();
        const float yGreen  = bar.getY()      + (1.0f - ampToNorm(dbToAmp(-12.0f))) * bar.getHeight();
        const float yFloor  = bar.getBottom();

        juce::ColourGradient grad(Colors::vuRed(),    bar.getCentreX(), yRed,
                                  Colors::vuGreen(),  bar.getCentreX(), yFloor,
                                  false);
        grad.addColour(juce::jlimit(0.0f, 1.0f, (yYellow - yRed) / std::max(1.0f, (yFloor - yRed))),
                       Colors::vuYellow());
        grad.addColour(juce::jlimit(0.0f, 1.0f, (yGreen  - yRed) / std::max(1.0f, (yFloor - yRed))),
                       Colors::vuGreen());

        const int activeY = bar.getBottom() - activeH;
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(bar.getX(), activeY, bar.getWidth(), activeH);
        g.setGradientFill(grad);
        g.fillRect(bar);
    }

    if (peakAmp > 1.0e-4f) {
        const float pn = ampToNorm(peakAmp);
        const int py   = bar.getBottom() - static_cast<int>(std::round(pn * bar.getHeight()));
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.fillRect(bar.getX(), juce::jlimit(bar.getY(), bar.getBottom() - 1, py), bar.getWidth(), 1);
    }

    g.setColour(juce::Colour(0xFF1E293B));
    g.drawRect(bar, 1);
}

void PeakMeterDual::paint(juce::Graphics& g)
{
    auto area = getLocalBounds();
    if (area.getWidth() < 4 || area.getHeight() < 4) return;

    constexpr int kBarWidth = 8;
    constexpr int kGap      = 2;

    auto barL = juce::Rectangle<int>(area.getX(),                        area.getY(), kBarWidth, area.getHeight());
    auto barR = juce::Rectangle<int>(area.getX() + kBarWidth + kGap,     area.getY(), kBarWidth, area.getHeight());

    drawChannel(g, barL, m_displayL, m_peakHoldL);
    drawChannel(g, barR, m_displayR, m_peakHoldR);
}

GainReductionMeter::GainReductionMeter()
{
    setOpaque(false);
    setInterceptsMouseClicks(false, false);
    startTimerHz(kTimerHz);
}

void GainReductionMeter::setGainReductionDb(float gainReductionDb) noexcept
{
    m_targetGrDb = juce::jlimit(0.0f, kMaxGrDb, gainReductionDb);
}

void GainReductionMeter::timerCallback()
{
    if (m_targetGrDb >= m_displayGrDb)
        m_displayGrDb = m_targetGrDb;
    else
        m_displayGrDb = std::max(m_targetGrDb,
                                 m_displayGrDb - kBarDecayDbPerSec * kTimerDt);

    repaint();
}

void GainReductionMeter::paint(juce::Graphics& g)
{
    auto area = getLocalBounds();
    if (area.getWidth() < 2 || area.getHeight() < 2) return;

    g.setColour(juce::Colour(0xFF0B0B12));
    g.fillRect(area);

    const float norm = juce::jlimit(0.0f, 1.0f, m_displayGrDb / kMaxGrDb);
    const int   h    = static_cast<int>(std::round(norm * area.getHeight()));
    if (h > 0) {
        g.setColour(juce::Colour(0xFFFB923C)); // orange
        g.fillRect(area.getX(), area.getY(), area.getWidth(), h);
    }

    g.setColour(juce::Colour(0xFF1E293B));
    g.drawRect(area, 1);
}

} // namespace BeatMate::UI
