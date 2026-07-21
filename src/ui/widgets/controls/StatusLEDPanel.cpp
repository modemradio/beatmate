#include "StatusLEDPanel.h"
#include "../../styles/ColorPalette.h"
namespace BeatMate::UI {

StatusLEDPanel::StatusLEDPanel() {
    m_leds.resize(LED_COUNT);
    m_leds[KEY]    = {false, false, juce::Colour(0xFF00FFA3), "KEY", {}};
    m_leds[SYNC]   = {false, false, juce::Colour(0xFF4A90E2), "SYNC", {}};
    m_leds[QUANT]  = {false, false, juce::Colour(0xFFFFD700), "QUANT", {}};
    m_leds[LOOP]   = {false, false, juce::Colour(0xFFFF8800), "LOOP", {}};
    m_leds[REC]    = {false, false, juce::Colour(0xFFFF3838), "REC", {}};
    m_leds[SLIP]   = {false, false, juce::Colour(0xFFB048FF), "SLIP", {}};
    m_leds[MASTER] = {false, false, juce::Colour(0xFFFF6B9D), "MST", {}};
    startTimer(500);
    setSize(280, 24);
}

void StatusLEDPanel::setLED(LEDId led, bool active) {
    if (led < LED_COUNT) { m_leds[led].active = active; repaint(); }
}

void StatusLEDPanel::setLEDFlashing(LEDId led, bool flashing) {
    if (led < LED_COUNT) m_leds[led].flashing = flashing;
}

bool StatusLEDPanel::isLEDActive(LEDId led) const {
    return led < LED_COUNT ? m_leds[led].active : false;
}

void StatusLEDPanel::timerCallback() {
    m_flashPhase = !m_flashPhase;
    bool needRepaint = false;
    for (auto& led : m_leds) if (led.flashing) needRepaint = true;
    if (needRepaint) repaint();
}

void StatusLEDPanel::resized() {
    int count = LED_COUNT;
    if (m_horizontal) {
        int ledW = getWidth() / count;
        for (int i = 0; i < count; ++i) {
            m_leds[i].bounds = juce::Rectangle<int>(i * ledW, 0, ledW, getHeight());
        }
    } else {
        int ledH = getHeight() / count;
        for (int i = 0; i < count; ++i) {
            m_leds[i].bounds = juce::Rectangle<int>(0, i * ledH, getWidth(), ledH);
        }
    }
}

void StatusLEDPanel::paint(juce::Graphics& g) {
    g.fillAll(Colors::bgDark());

    for (int i = 0; i < LED_COUNT; ++i) {
        auto& led = m_leds[i];
        auto b = led.bounds.reduced(2);
        bool showActive = led.active && (!led.flashing || m_flashPhase);

        g.setColour(Colors::bgDarker());
        g.fillRoundedRectangle(b.toFloat(), 3.0f);

        float dotSize = std::min(b.getHeight() - 4.0f, 8.0f);
        float dotX = b.getX() + 4.0f;
        float dotY = b.getCentreY() - dotSize * 0.5f;

        if (showActive) {
            g.setColour(led.color.withAlpha(0.2f));
            g.fillRoundedRectangle(b.toFloat(), 3.0f);
            g.setColour(led.color);
            g.fillEllipse(dotX, dotY, dotSize, dotSize);
            g.setColour(led.color.withAlpha(0.4f));
            g.fillEllipse(dotX - 2, dotY - 2, dotSize + 4, dotSize + 4);
        } else {
            g.setColour(led.color.withAlpha(0.15f));
            g.fillEllipse(dotX, dotY, dotSize, dotSize);
        }

        g.setColour(showActive ? led.color : Colors::textDim());
        g.setFont(juce::Font(7.5f, juce::Font::bold));
        g.drawText(led.label, b.getX() + (int)dotSize + 6, b.getY(), b.getWidth() - (int)dotSize - 8,
                   b.getHeight(), juce::Justification::centredLeft);

        g.setColour(Colors::border());
        g.drawRoundedRectangle(b.toFloat(), 3.0f, 0.5f);
    }
}

void StatusLEDPanel::mouseDown(const juce::MouseEvent& e) {
    for (int i = 0; i < LED_COUNT; ++i) {
        if (m_leds[i].bounds.contains(e.getPosition())) {
            m_listeners.call([i](Listener& l) { l.ledClicked(static_cast<LEDId>(i)); });
            break;
        }
    }
}

} // namespace BeatMate::UI
