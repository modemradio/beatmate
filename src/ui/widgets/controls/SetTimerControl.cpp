#include "SetTimerControl.h"
#include "../../styles/ColorPalette.h"
#include <cmath>
namespace BeatMate::UI {

SetTimerControl::SetTimerControl() { setSize(160, 50); }

void SetTimerControl::start() {
    m_running = true;
    m_startTime = std::chrono::steady_clock::now();
    m_pausedElapsed = m_elapsed;
    m_warningFired = false;
    m_expiredFired = false;
    startTimer(100);
    m_listeners.call([](Listener& l) { l.timerStarted(); });
    repaint();
}

void SetTimerControl::stop() {
    m_running = false;
    stopTimer();
    m_listeners.call([](Listener& l) { l.timerStopped(); });
    repaint();
}

void SetTimerControl::reset() {
    m_running = false;
    m_elapsed = 0;
    m_pausedElapsed = 0;
    m_warningFired = false;
    m_expiredFired = false;
    stopTimer();
    repaint();
}

void SetTimerControl::timerCallback() {
    if (!m_running) return;
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - m_startTime).count();
    m_elapsed = m_pausedElapsed + dt;

    if (m_targetDuration > 0) {
        double remaining = m_targetDuration - m_elapsed;
        if (remaining <= m_warningThreshold && !m_warningFired) {
            m_warningFired = true;
            m_listeners.call([](Listener& l) { l.timerWarning(); });
        }
        if (remaining <= 0 && !m_expiredFired) {
            m_expiredFired = true;
            m_listeners.call([](Listener& l) { l.timerExpired(); });
        }
    }
    repaint();
}

void SetTimerControl::paint(juce::Graphics& g) {
    int w = getWidth(), h = getHeight();
    g.fillAll(Colors::bgDark());
    g.setColour(Colors::border());
    g.drawRoundedRectangle(0.5f, 0.5f, w - 1.0f, h - 1.0f, 4.0f, 1.0f);

    if (m_targetDuration > 0) {
        float progress = (float)(m_elapsed / m_targetDuration);
        progress = juce::jlimit(0.0f, 1.0f, progress);
        int barH = 4;
        int barY = h - barH - 2;

        g.setColour(Colors::bgLighter());
        g.fillRoundedRectangle(4.0f, (float)barY, (float)(w - 8), (float)barH, 2.0f);

        juce::Colour barColor = Colors::primary();
        double remaining = m_targetDuration - m_elapsed;
        if (remaining <= 0) barColor = Colors::error();
        else if (remaining <= m_warningThreshold) barColor = Colors::warning();

        g.setColour(barColor);
        g.fillRoundedRectangle(4.0f, (float)barY, (float)((w - 8) * progress), (float)barH, 2.0f);
    }

    int iconSize = 12;
    int iconX = 8, iconY = (h - iconSize) / 2 - (m_targetDuration > 0 ? 3 : 0);
    g.setColour(m_running ? Colors::success() : Colors::textMuted());
    if (m_running) {
        g.fillRect(iconX, iconY, 4, iconSize);
        g.fillRect(iconX + 7, iconY, 4, iconSize);
    } else {
        juce::Path tri;
        tri.addTriangle((float)iconX, (float)iconY, (float)iconX, (float)(iconY + iconSize),
                        (float)(iconX + iconSize), (float)(iconY + iconSize / 2));
        g.fillPath(tri);
    }

    juce::String elapsedStr = formatDuration(m_elapsed);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(18.0f, juce::Font::bold));
    g.drawText(elapsedStr, iconX + iconSize + 6, 2, w - iconX - iconSize - 12,
               h - (m_targetDuration > 0 ? 10 : 4), juce::Justification::centredLeft);

    if (m_targetDuration > 0) {
        double remaining = std::max(0.0, m_targetDuration - m_elapsed);
        juce::Colour remColor = Colors::textMuted();
        if (remaining <= 0) remColor = Colors::error();
        else if (remaining <= m_warningThreshold) remColor = Colors::warning();
        g.setColour(remColor);
        g.setFont(juce::Font(9.0f));
        juce::String remStr = "-" + formatDuration(remaining);
        g.drawText(remStr, w - 60, 2, 56, 14, juce::Justification::right);
    }

    g.setColour(Colors::textDim());
    g.setFont(juce::Font(8.0f));
    g.drawText("SET", w - 28, h - 16, 24, 12, juce::Justification::right);
}

void SetTimerControl::mouseDown(const juce::MouseEvent&) {
    if (m_running) stop(); else start();
}

juce::String SetTimerControl::formatDuration(double secs) const {
    int h = (int)secs / 3600;
    int m = ((int)secs % 3600) / 60;
    int s = (int)secs % 60;
    if (h > 0) return juce::String::formatted("%d:%02d:%02d", h, m, s);
    return juce::String::formatted("%02d:%02d", m, s);
}

} // namespace BeatMate::UI
