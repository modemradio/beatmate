#pragma once
#include "ProVUMeterWidget.h"
namespace BeatMate::UI {

// ProDualVUMeterWidget - Stereo dual VU meter (L/R)

class ProDualVUMeterWidget : public juce::Component {
public:
    ProDualVUMeterWidget();
    ~ProDualVUMeterWidget() override = default;

    void setLevels(float left, float right);
    void setLevelsDb(float leftDb, float rightDb);
    void setPeakHold(bool enabled);
    void setSegmented(bool segmented);
    void setShowScale(bool show);
    void setChannelLabels(const juce::String& left, const juce::String& right);

    float getLeftLevel() const { return m_leftMeter.getLevel(); }
    float getRightLevel() const { return m_rightMeter.getLevel(); }

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    ProVUMeterWidget m_leftMeter, m_rightMeter;
    juce::String m_leftLabel = "L", m_rightLabel = "R";
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProDualVUMeterWidget)
};

} // namespace BeatMate::UI
