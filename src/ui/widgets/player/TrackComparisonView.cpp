#include "TrackComparisonView.h"
#include "../../styles/ColorPalette.h"
#include <cmath>
namespace BeatMate::UI {

TrackComparisonView::TrackComparisonView() { setSize(500, 280); }

void TrackComparisonView::setTrackA(const TrackComparisonData& data) { m_trackA = data; m_hasA = true; repaint(); }
void TrackComparisonView::setTrackB(const TrackComparisonData& data) { m_trackB = data; m_hasB = true; repaint(); }
void TrackComparisonView::clearTracks() { m_hasA = m_hasB = false; repaint(); }

float TrackComparisonView::computeBPMMatch() const {
    if (m_trackA.bpm <= 0 || m_trackB.bpm <= 0) return -1.0f;
    double diff = std::abs(m_trackA.bpm - m_trackB.bpm);
    double halfDiff = std::abs(m_trackA.bpm - m_trackB.bpm * 2.0);
    double doubleDiff = std::abs(m_trackA.bpm * 2.0 - m_trackB.bpm);
    diff = std::min({diff, halfDiff, doubleDiff});
    if (diff < 0.5) return 1.0f;
    if (diff < 2.0) return 0.8f;
    if (diff < 5.0) return 0.5f;
    return 0.2f;
}

float TrackComparisonView::computeKeyMatch() const {
    if (m_trackA.key.isEmpty() || m_trackB.key.isEmpty()) return -1.0f;
    if (m_trackA.key == m_trackB.key) return 1.0f;
    return 0.5f;
}

float TrackComparisonView::computeEnergyMatch() const {
    float diff = std::abs(m_trackA.energy - m_trackB.energy);
    return 1.0f - diff;
}

juce::Colour TrackComparisonView::matchColor(float score) const {
    if (score < 0) return Colors::textDim();
    if (score >= 0.8f) return juce::Colour(0xFF00FF66);
    if (score >= 0.5f) return juce::Colour(0xFFFFDD00);
    return juce::Colour(0xFFFF3333);
}

void TrackComparisonView::drawComparisonRow(juce::Graphics& g, int y, int h, const juce::String& label,
                                             const juce::String& valA, const juce::String& valB,
                                             float matchScore) const {
    int w = getWidth();
    int colW = (w - 80) / 2;

    if ((y / h) % 2 == 0) {
        g.setColour(juce::Colour(0xFF141414));
        g.fillRect(0, y, w, h);
    }

    g.setColour(Colors::textMuted());
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawText(label, 4, y, 72, h, juce::Justification::centredRight);

    g.setColour(Colors::textPrimary());
    g.setFont(juce::Font(10.0f));
    g.drawText(valA, 80, y, colW, h, juce::Justification::centred);

    if (matchScore >= 0) {
        float dotX = 80.0f + colW;
        float dotY = y + h * 0.5f - 4.0f;
        g.setColour(matchColor(matchScore));
        g.fillEllipse(dotX - 4, dotY, 8, 8);
    } else {
        g.setColour(Colors::border());
        g.drawVerticalLine(80 + colW, (float)y + 2, (float)(y + h - 2));
    }

    g.setColour(Colors::textPrimary());
    g.drawText(valB, 80 + colW + 8, y, colW - 8, h, juce::Justification::centred);
}

void TrackComparisonView::paint(juce::Graphics& g) {
    int w = getWidth(), h = getHeight();
    ProDraw::viewBackground(g, getWidth(), getHeight());

    if (!m_hasA && !m_hasB) {
        g.setColour(Colors::textDim());
        g.setFont(juce::Font(12.0f));
        g.drawText("Load two tracks to compare", 0, 0, w, h, juce::Justification::centred);
        return;
    }

    int colW = (w - 80) / 2;
    g.setColour(Colors::bgMedium());
    g.fillRect(0, 0, w, 28);

    g.setColour(Colors::accent());
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.drawText("DECK A", 80, 0, colW, 28, juce::Justification::centred);
    g.drawText("DECK B", 80 + colW, 0, colW, 28, juce::Justification::centred);

    g.setColour(Colors::textDim());
    g.setFont(juce::Font(8.0f));
    g.drawText("COMPARE", 4, 0, 72, 28, juce::Justification::centredRight);

    int rowH = 22;
    int y = 30;

    drawComparisonRow(g, y, rowH, "Title", m_hasA ? m_trackA.title : "-", m_hasB ? m_trackB.title : "-"); y += rowH;
    drawComparisonRow(g, y, rowH, "Artist", m_hasA ? m_trackA.artist : "-", m_hasB ? m_trackB.artist : "-"); y += rowH;
    drawComparisonRow(g, y, rowH, "BPM",
        m_hasA ? juce::String(m_trackA.bpm, 1) : "-",
        m_hasB ? juce::String(m_trackB.bpm, 1) : "-",
        (m_hasA && m_hasB) ? computeBPMMatch() : -1.0f); y += rowH;
    drawComparisonRow(g, y, rowH, "Key",
        m_hasA ? m_trackA.key : "-",
        m_hasB ? m_trackB.key : "-",
        (m_hasA && m_hasB) ? computeKeyMatch() : -1.0f); y += rowH;
    drawComparisonRow(g, y, rowH, "Energy",
        m_hasA ? juce::String(m_trackA.energy * 10.0f, 1) : "-",
        m_hasB ? juce::String(m_trackB.energy * 10.0f, 1) : "-",
        (m_hasA && m_hasB) ? computeEnergyMatch() : -1.0f); y += rowH;
    drawComparisonRow(g, y, rowH, "Duration",
        m_hasA ? juce::String::formatted("%d:%02d", (int)m_trackA.durationSeconds / 60, (int)m_trackA.durationSeconds % 60) : "-",
        m_hasB ? juce::String::formatted("%d:%02d", (int)m_trackB.durationSeconds / 60, (int)m_trackB.durationSeconds % 60) : "-"); y += rowH;
    drawComparisonRow(g, y, rowH, "Genre",
        m_hasA ? m_trackA.genre : "-", m_hasB ? m_trackB.genre : "-"); y += rowH;
    drawComparisonRow(g, y, rowH, "Year",
        m_hasA && m_trackA.year > 0 ? juce::String(m_trackA.year) : "-",
        m_hasB && m_trackB.year > 0 ? juce::String(m_trackB.year) : "-"); y += rowH;
    drawComparisonRow(g, y, rowH, "LUFS",
        m_hasA ? juce::String(m_trackA.loudnessLUFS, 1) + " dB" : "-",
        m_hasB ? juce::String(m_trackB.loudnessLUFS, 1) + " dB" : "-"); y += rowH;

    if (m_hasA && m_hasB) {
        float bpmMatch = computeBPMMatch();
        float keyMatch = computeKeyMatch();
        float energyMatch = computeEnergyMatch();
        int validCount = 0;
        float total = 0;
        if (bpmMatch >= 0) { total += bpmMatch; validCount++; }
        if (keyMatch >= 0) { total += keyMatch; validCount++; }
        total += energyMatch; validCount++;
        float overall = validCount > 0 ? total / validCount : 0;

        y += 4;
        g.setColour(Colors::bgMedium());
        g.fillRect(0, y, w, 28);
        g.setColour(Colors::textMuted());
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText("OVERALL MATCH", 4, y, 120, 28, juce::Justification::centredLeft);

        g.setColour(matchColor(overall));
        g.setFont(juce::Font(16.0f, juce::Font::bold));
        g.drawText(juce::String((int)(overall * 100)) + "%", w - 80, y, 76, 28, juce::Justification::centredRight);

        g.setColour(Colors::bgLighter());
        g.fillRoundedRectangle(130.0f, y + 10.0f, w - 220.0f, 8.0f, 3.0f);
        g.setColour(matchColor(overall));
        g.fillRoundedRectangle(130.0f, y + 10.0f, (w - 220.0f) * overall, 8.0f, 3.0f);
    }

    g.setColour(Colors::border());
    g.drawRect(getLocalBounds(), 1);
}

} // namespace BeatMate::UI
