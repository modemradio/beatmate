#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

namespace BeatMate::UI {

class PeakMeterDual : public juce::Component, public juce::Timer
{
public:
    PeakMeterDual();
    ~PeakMeterDual() override = default;

    // Set the raw target levels (0..1, linear amplitude). Thread-safe (UI thread).
    void setLevels(float left, float right) noexcept;

    void paint(juce::Graphics& g) override;
    void timerCallback() override;

private:
    float m_targetL { 0.0f };
    float m_targetR { 0.0f };

    // Smoothed, decaying display values (linear 0..1 amplitude).
    float m_displayL { 0.0f };
    float m_displayR { 0.0f };

    // Peak-hold values (linear 0..1 amplitude).
    float m_peakHoldL { 0.0f };
    float m_peakHoldR { 0.0f };

    static constexpr float kFloorDb { -60.0f };

    static float ampToNorm(float amp01) noexcept;
    void  drawChannel(juce::Graphics& g, juce::Rectangle<int> bar,
                      float displayAmp, float peakAmp) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PeakMeterDual)
};

// GainReductionMeter — narrow vertical meter showing master compressor GR (dB).
class GainReductionMeter : public juce::Component, public juce::Timer
{
public:
    GainReductionMeter();
    ~GainReductionMeter() override = default;

    // Set current gain reduction in dB (positive value, 0..12). Thread-safe.
    void setGainReductionDb(float gainReductionDb) noexcept;

    void paint(juce::Graphics& g) override;
    void timerCallback() override;

private:
    float m_targetGrDb  { 0.0f };
    float m_displayGrDb { 0.0f };

    static constexpr float kMaxGrDb { 12.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GainReductionMeter)
};

} // namespace BeatMate::UI
