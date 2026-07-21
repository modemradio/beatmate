#include "EnergyCurveEditor.h"

#include <algorithm>

namespace BeatMate::UI::Widgets {

namespace {
constexpr float kHandleR = 7.0f;
constexpr float kEnergyMax = 10.0f;
constexpr float kOutlierDelta = 1.5f;
}

EnergyCurveEditor::EnergyCurveEditor() {
    setInterceptsMouseClicks(true, false);
}

void EnergyCurveEditor::setTracks(const std::vector<Models::Track>& tracks) {
    m_tracks = tracks;
    repaint();
}

void EnergyCurveEditor::setControlPoints(const std::vector<float>& y0to10) {
    if (y0to10.size() != 4) return;
    m_cp = y0to10;
    for (auto& v : m_cp) v = juce::jlimit(0.0f, kEnergyMax, v);
    repaint();
}

juce::Rectangle<float> EnergyCurveEditor::plotBounds() const {
    return getLocalBounds().toFloat().reduced(16.0f, 10.0f);
}

juce::Point<float> EnergyCurveEditor::cpScreen(int i) const {
    auto pb = plotBounds();
    const int n = (int)m_cp.size();
    float x = pb.getX() + (pb.getWidth() * (float)i / (float)(n - 1));
    float y = pb.getBottom() - (pb.getHeight() * (m_cp[(size_t)i] / kEnergyMax));
    return { x, y };
}

int EnergyCurveEditor::hitTestControlPoint(juce::Point<float> p) const {
    for (int i = 0; i < (int)m_cp.size(); ++i) {
        if (cpScreen(i).getDistanceFrom(p) <= kHandleR + 2.0f) return i;
    }
    return -1;
}

float EnergyCurveEditor::sampleCurveAt(float normalizedX) const {
    const int n = (int)m_cp.size();
    if (n == 0) return 0.0f;
    float x = juce::jlimit(0.0f, 1.0f, normalizedX) * (float)(n - 1);
    int i = (int)std::floor(x);
    int j = std::min(i + 1, n - 1);
    float t = x - (float)i;
    return juce::jmap(t, 0.0f, 1.0f, m_cp[(size_t)i], m_cp[(size_t)j]);
}

void EnergyCurveEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(22, 24, 34));

    auto pb = plotBounds();

    g.setColour(juce::Colours::white.withAlpha(0.08f));
    for (int e = 0; e <= 10; e += 2) {
        float y = pb.getBottom() - pb.getHeight() * ((float)e / kEnergyMax);
        g.drawHorizontalLine((int)y, pb.getX(), pb.getRight());
    }

    juce::Path curve;
    const int steps = 80;
    for (int s = 0; s <= steps; ++s) {
        float t = (float)s / (float)steps;
        float y = pb.getBottom() - pb.getHeight() * (sampleCurveAt(t) / kEnergyMax);
        float x = pb.getX() + pb.getWidth() * t;
        if (s == 0) curve.startNewSubPath(x, y);
        else        curve.lineTo(x, y);
    }

    juce::ColourGradient grad(juce::Colour::fromRGB(80, 200, 240),  pb.getX(), pb.getBottom(),
                              juce::Colour::fromRGB(240, 140,  60), pb.getRight(), pb.getY(),
                              false);
    g.setGradientFill(grad);
    g.strokePath(curve, juce::PathStrokeType(2.5f));

    for (int i = 0; i < (int)m_cp.size(); ++i) {
        auto p = cpScreen(i);
        g.setColour(juce::Colours::black);
        g.fillEllipse(p.x - kHandleR, p.y - kHandleR, kHandleR * 2, kHandleR * 2);
        g.setColour(juce::Colours::white);
        g.fillEllipse(p.x - kHandleR + 2, p.y - kHandleR + 2,
                      (kHandleR - 2) * 2, (kHandleR - 2) * 2);
    }

    if (!m_tracks.empty()) {
        const size_t n = m_tracks.size();
        for (size_t i = 0; i < n; ++i) {
            float t = (n == 1) ? 0.5f : (float)i / (float)(n - 1);
            float target = sampleCurveAt(t);
            float actual = juce::jlimit(0.0f, kEnergyMax, (float)m_tracks[i].energy);
            float delta = std::abs(actual - target);

            float x = pb.getX() + pb.getWidth() * t;
            float y = pb.getBottom() - pb.getHeight() * (actual / kEnergyMax);

            juce::Colour dotCol = (delta > kOutlierDelta)
                ? juce::Colour::fromRGB(230, 70, 70)
                : juce::Colour::fromRGB(80, 220, 160);
            g.setColour(dotCol.withAlpha(0.85f));
            g.fillEllipse(x - 4.0f, y - 4.0f, 8.0f, 8.0f);

            if (delta > 0.1f) {
                float ty = pb.getBottom() - pb.getHeight() * (target / kEnergyMax);
                g.setColour(dotCol.withAlpha(0.35f));
                g.drawLine(x, y, x, ty, 1.0f);
            }
        }
    }

    g.setColour(juce::Colours::white.withAlpha(0.55f));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText("E", (int)pb.getX() - 14, (int)pb.getY() - 2, 12, 12,
               juce::Justification::centredLeft);
    g.drawText("time", (int)pb.getRight() - 30, (int)pb.getBottom() + 2, 30, 12,
               juce::Justification::centredLeft);
}

void EnergyCurveEditor::mouseDown(const juce::MouseEvent& e) {
    m_dragIndex = hitTestControlPoint(e.position);
}

void EnergyCurveEditor::mouseDrag(const juce::MouseEvent& e) {
    if (m_dragIndex < 0) return;
    auto pb = plotBounds();
    float normY = juce::jlimit(0.0f, 1.0f,
        (pb.getBottom() - e.position.y) / pb.getHeight());
    m_cp[(size_t)m_dragIndex] = normY * kEnergyMax;
    if (onCurveChanged) onCurveChanged();
    repaint();
}

void EnergyCurveEditor::mouseUp(const juce::MouseEvent&) {
    m_dragIndex = -1;
}

}
