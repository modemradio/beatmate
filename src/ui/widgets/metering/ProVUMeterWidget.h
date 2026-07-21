#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {


class ProVUMeterWidget : public juce::Component, public juce::Timer {
public:
    enum Orientation { Vertical, Horizontal };

    ProVUMeterWidget();
    ~ProVUMeterWidget() override = default;

    void setLevel(float level);
    void setLevelDb(float db);
    float getLevel() const { return m_level; }

    void setPeakHold(bool enabled) { m_peakHoldEnabled = enabled; }
    void setOrientation(Orientation o) { m_orientation = o; repaint(); }
    void setSegmented(bool segmented) { m_segmented = segmented; repaint(); }
    void setShowScale(bool show) { m_showScale = show; repaint(); }
    void setMeterWidth(int width) { m_meterWidth = width; repaint(); }

    void setYellowThreshold(float t) { m_yellowThreshold = t; }
    void setRedThreshold(float t) { m_redThreshold = t; }

    void paint(juce::Graphics& g) override;
    void timerCallback() override;

private:
    float dbToNormalized(float db) const;

    float m_level = 0.0f;
    float m_peakLevel = 0.0f;
    float m_peakDecay = 0.0f;
    float m_smoothLevel = 0.0f;
    bool m_peakHoldEnabled = true;
    bool m_clipping = false;
    bool m_segmented = false;
    bool m_showScale = true;
    Orientation m_orientation = Vertical;
    int m_meterWidth = 12;
    float m_yellowThreshold = 0.6f;
    float m_redThreshold = 0.85f;
    int m_peakHoldCounter = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProVUMeterWidget)
};

}
