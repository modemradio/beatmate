#include "ProDualVUMeterWidget.h"
#include "../../styles/ColorPalette.h"
namespace BeatMate::UI {

ProDualVUMeterWidget::ProDualVUMeterWidget() {
    addAndMakeVisible(m_leftMeter);
    addAndMakeVisible(m_rightMeter);
    m_leftMeter.setMeterWidth(10);
    m_rightMeter.setMeterWidth(10);
    m_leftMeter.setShowScale(true);
    m_rightMeter.setShowScale(false);
    setSize(60, 200);
}

void ProDualVUMeterWidget::setLevels(float left, float right) {
    m_leftMeter.setLevel(left);
    m_rightMeter.setLevel(right);
}

void ProDualVUMeterWidget::setLevelsDb(float leftDb, float rightDb) {
    m_leftMeter.setLevelDb(leftDb);
    m_rightMeter.setLevelDb(rightDb);
}

void ProDualVUMeterWidget::setPeakHold(bool enabled) {
    m_leftMeter.setPeakHold(enabled);
    m_rightMeter.setPeakHold(enabled);
}

void ProDualVUMeterWidget::setSegmented(bool segmented) {
    m_leftMeter.setSegmented(segmented);
    m_rightMeter.setSegmented(segmented);
}

void ProDualVUMeterWidget::setShowScale(bool show) {
    m_leftMeter.setShowScale(show);
    m_rightMeter.setShowScale(false);
}

void ProDualVUMeterWidget::setChannelLabels(const juce::String& left, const juce::String& right) {
    m_leftLabel = left;
    m_rightLabel = right;
    repaint();
}

void ProDualVUMeterWidget::resized() {
    int w = getWidth(), h = getHeight();
    int labelH = 14;
    int halfW = w / 2;
    m_leftMeter.setBounds(0, 0, halfW, h - labelH);
    m_rightMeter.setBounds(halfW, 0, w - halfW, h - labelH);
}

void ProDualVUMeterWidget::paint(juce::Graphics& g) {
    g.fillAll(Colors::bgDarkest());
    int w = getWidth(), h = getHeight();
    g.setFont(juce::Font(8.0f, juce::Font::bold));
    g.setColour(Colors::textMuted());
    g.drawText(m_leftLabel, 0, h - 14, w / 2, 14, juce::Justification::centred);
    g.drawText(m_rightLabel, w / 2, h - 14, w / 2, 14, juce::Justification::centred);
    g.setColour(Colors::border());
    g.drawVerticalLine(w / 2, 10.0f, (float)(h - 14));
}

} // namespace BeatMate::UI
