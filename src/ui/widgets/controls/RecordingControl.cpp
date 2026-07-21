#include "RecordingControl.h"
#include "../../styles/ColorPalette.h"
namespace BeatMate::UI {

RecordingControl::RecordingControl() { startTimer(500); setSize(180, 36); }

void RecordingControl::setRecording(bool recording) {
    m_recording = recording;
    if (!recording) { m_elapsed = 0; m_fileSize = 0; }
    repaint();
}

void RecordingControl::setElapsedSeconds(double seconds) { m_elapsed = seconds; repaint(); }
void RecordingControl::setFileSize(int64_t bytes) { m_fileSize = bytes; repaint(); }

void RecordingControl::timerCallback() {
    if (m_recording) { m_flashPhase = !m_flashPhase; repaint(); }
}

void RecordingControl::resized() {}

void RecordingControl::paint(juce::Graphics& g) {
    int w = getWidth(), h = getHeight();
    g.setColour(m_recording ? juce::Colour(0xFF1A0000) : Colors::bgDark());
    g.fillRoundedRectangle(0.0f, 0.0f, (float)w, (float)h, 4.0f);

    float btnSize = h - 8.0f;
    float btnX = 4.0f, btnY = 4.0f;
    if (m_recording) {
        juce::Colour recColor = m_flashPhase ? juce::Colour(0xFFFF0000) : juce::Colour(0xFFCC0000);
        g.setColour(recColor);
        g.fillEllipse(btnX, btnY, btnSize, btnSize);
        g.setColour(juce::Colour(0xFFFF0000).withAlpha(0.3f));
        g.fillEllipse(btnX - 2, btnY - 2, btnSize + 4, btnSize + 4);
    } else {
        g.setColour(juce::Colour(0xFF660000));
        g.fillEllipse(btnX, btnY, btnSize, btnSize);
        g.setColour(juce::Colour(0xFFAA0000));
        g.drawEllipse(btnX, btnY, btnSize, btnSize, 1.5f);
    }

    float textX = btnX + btnSize + 8;
    g.setColour(m_recording ? juce::Colour(0xFFFF4444) : Colors::textMuted());
    g.setFont(juce::Font(14.0f, juce::Font::bold));
    g.drawText(formatTime(m_elapsed), (int)textX, 0, w - (int)textX - 4, h / 2 + 4, juce::Justification::centredLeft);

    if (m_recording && m_fileSize > 0) {
        g.setColour(Colors::textDim());
        g.setFont(juce::Font(9.0f));
        g.drawText(formatSize(m_fileSize), (int)textX, h / 2 - 2, w - (int)textX - 4, h / 2 + 2, juce::Justification::centredLeft);
    }

    if (!m_recording) {
        g.setColour(Colors::textDim());
        g.setFont(juce::Font(9.0f));
        g.drawText("REC", (int)textX, h / 2 - 2, 30, h / 2, juce::Justification::centredLeft);
    }

    g.setColour(m_recording ? juce::Colour(0xFF440000) : Colors::border());
    g.drawRoundedRectangle(0.5f, 0.5f, w - 1.0f, h - 1.0f, 4.0f, 1.0f);
}

void RecordingControl::mouseDown(const juce::MouseEvent&) {
    m_listeners.call([this](Listener& l) { l.recordToggled(!m_recording); });
}

juce::String RecordingControl::formatTime(double seconds) const {
    int h = (int)seconds / 3600;
    int m = ((int)seconds % 3600) / 60;
    int s = (int)seconds % 60;
    if (h > 0) return juce::String::formatted("%d:%02d:%02d", h, m, s);
    return juce::String::formatted("%02d:%02d", m, s);
}

juce::String RecordingControl::formatSize(int64_t bytes) const {
    if (bytes < 1024) return juce::String(bytes) + " B";
    if (bytes < 1024 * 1024) return juce::String(bytes / 1024) + " KB";
    if (bytes < 1024LL * 1024 * 1024) return juce::String::formatted("%.1f MB", bytes / (1024.0 * 1024.0));
    return juce::String::formatted("%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
}

} // namespace BeatMate::UI
