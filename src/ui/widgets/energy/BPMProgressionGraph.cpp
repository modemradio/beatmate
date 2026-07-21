#include "BPMProgressionGraph.h"

#include <algorithm>

namespace BeatMate::UI::Widgets {

BPMProgressionGraph::BPMProgressionGraph() {
    setInterceptsMouseClicks(true, false);
}

void BPMProgressionGraph::setTracks(const std::vector<Models::Track>& tracks) {
    m_tracks = tracks;
    m_hoverIndex = -1;
    repaint();
}

void BPMProgressionGraph::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(18, 20, 30));
    auto pb = getLocalBounds().toFloat().reduced(20.0f, 14.0f);

    if (m_tracks.size() < 2) {
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
        g.drawText("BPM progression (add tracks to see)",
                   pb.toNearestInt(), juce::Justification::centred);
        return;
    }

    double minBpm = 1e9, maxBpm = -1e9;
    for (const auto& t : m_tracks) {
        minBpm = std::min(minBpm, t.bpm);
        maxBpm = std::max(maxBpm, t.bpm);
    }
    if (maxBpm - minBpm < 10.0) {
        double mid = (maxBpm + minBpm) * 0.5;
        minBpm = mid - 5.0;
        maxBpm = mid + 5.0;
    }
    double range = maxBpm - minBpm;
    minBpm -= range * 0.1;
    maxBpm += range * 0.1;

    g.setColour(juce::Colours::white.withAlpha(0.08f));
    for (int i = 0; i < 5; ++i) {
        float y = pb.getY() + pb.getHeight() * (float)i / 4.0f;
        g.drawHorizontalLine((int)y, pb.getX(), pb.getRight());
    }

    auto mapX = [&](size_t i) {
        float t = (m_tracks.size() == 1) ? 0.5f
                    : (float)i / (float)(m_tracks.size() - 1);
        return pb.getX() + pb.getWidth() * t;
    };
    auto mapY = [&](double bpm) {
        float t = (float)((bpm - minBpm) / (maxBpm - minBpm));
        return pb.getBottom() - pb.getHeight() * t;
    };

    for (size_t i = 1; i < m_tracks.size(); ++i) {
        float x0 = mapX(i - 1), x1 = mapX(i);
        float y0 = mapY(m_tracks[i - 1].bpm);
        float y1 = mapY(m_tracks[i].bpm);

        double delta = m_tracks[i].bpm - m_tracks[i - 1].bpm;
        bool isJump = std::abs(delta) > m_jumpThreshold;

        g.setColour(isJump
            ? juce::Colour::fromRGB(230, 70, 70)
            : juce::Colour::fromRGB(120, 180, 240));
        g.drawLine(x0, y0, x1, y1, isJump ? 2.5f : 1.5f);

        if (isJump) {
            juce::String txt = (delta > 0 ? "+" : "") + juce::String(delta, 1);
            float mx = (x0 + x1) * 0.5f;
            float my = (y0 + y1) * 0.5f - 8.0f;
            g.setColour(juce::Colour::fromRGB(255, 200, 200));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f).withStyle("Bold")));
            g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\x9a\xa0 ")) + txt,
                       (int)(mx - 30), (int)my, 60, 12,
                       juce::Justification::centred);
        }
    }

    for (size_t i = 0; i < m_tracks.size(); ++i) {
        float x = mapX(i), y = mapY(m_tracks[i].bpm);
        bool hovered = ((int)i == m_hoverIndex);
        float r = hovered ? 5.0f : 3.5f;
        g.setColour(juce::Colours::white);
        g.fillEllipse(x - r, y - r, r * 2, r * 2);
    }

    if (m_hoverIndex >= 0 && m_hoverIndex < (int)m_tracks.size()) {
        const auto& t = m_tracks[(size_t)m_hoverIndex];
        juce::String text = t.title.empty()
            ? juce::String("BPM ") + juce::String(t.bpm, 1)
            : juce::String(t.title) + " (" + juce::String(t.bpm, 1) + ")";
        float x = mapX((size_t)m_hoverIndex);
        g.setColour(juce::Colours::black.withAlpha(0.75f));
        g.fillRoundedRectangle(x + 8, pb.getY(), 180, 18, 3.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
        g.drawText(text, (int)(x + 12), (int)pb.getY(), 170, 18,
                   juce::Justification::centredLeft, true);
    }

    g.setColour(juce::Colours::white.withAlpha(0.55f));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
    g.drawText(juce::String(maxBpm, 0) + " BPM",
               (int)pb.getX() - 40, (int)pb.getY() - 4, 40, 10,
               juce::Justification::centredRight);
    g.drawText(juce::String(minBpm, 0),
               (int)pb.getX() - 40, (int)pb.getBottom() - 8, 40, 10,
               juce::Justification::centredRight);
}

void BPMProgressionGraph::mouseMove(const juce::MouseEvent& e) {
    auto pb = getLocalBounds().toFloat().reduced(20.0f, 14.0f);
    if (m_tracks.empty() || pb.getWidth() <= 0) return;
    float rel = juce::jlimit(0.0f, 1.0f, (e.position.x - pb.getX()) / pb.getWidth());
    int idx = (int)std::round(rel * (float)(m_tracks.size() - 1));
    if (idx != m_hoverIndex) {
        m_hoverIndex = idx;
        repaint();
    }
}

} // namespace BeatMate::UI::Widgets
