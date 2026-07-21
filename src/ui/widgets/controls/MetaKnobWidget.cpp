#include "MetaKnobWidget.h"
#include "../../styles/ColorPalette.h"
#include <cmath>
namespace BeatMate::UI {

MetaKnobWidget::MetaKnobWidget() { setSize(90, 110); setMouseCursor(juce::MouseCursor::PointingHandCursor); }

void MetaKnobWidget::setPosition(float position) {
    m_position = juce::jlimit(0.0f, 1.0f, position);
    m_listeners.call([this](Listener& l) { l.metaKnobChanged(m_position); });
    repaint();
}

void MetaKnobWidget::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::transparentBlack);
    int w = getWidth(), h = getHeight();
    int knobSize = std::min(w, h - 30);
    float cx = w * 0.5f, cy = knobSize * 0.5f + 4.0f;
    float outerRadius = knobSize * 0.5f - 4.0f;
    float innerRadius = outerRadius - 10.0f;

    float startAngle = -juce::MathConstants<float>::pi * 5.0f / 4.0f;
    float totalArc = juce::MathConstants<float>::pi * 3.0f / 2.0f;
    float posAngle = startAngle + m_position * totalArc;

    juce::Path bgArc;
    bgArc.addCentredArc(cx, cy, outerRadius, outerRadius, 0, startAngle, startAngle + totalArc, true);
    g.setColour(juce::Colour(0xFF1A1A1A));
    g.strokePath(bgArc, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (!m_arcs.empty()) {
        float ringWidth = std::max(2.0f, 8.0f / (float)m_arcs.size());
        for (int i = 0; i < (int)m_arcs.size(); ++i) {
            float r = outerRadius - i * (ringWidth + 1.0f);
            if (r < innerRadius) break;
            float endAngle = startAngle + m_arcs[i].value * totalArc;
            juce::Path arc;
            arc.addCentredArc(cx, cy, r, r, 0, startAngle, endAngle, true);
            g.setColour(m_arcs[i].color.withAlpha(0.6f));
            g.strokePath(arc, juce::PathStrokeType(ringWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    juce::Path posArc;
    posArc.addCentredArc(cx, cy, outerRadius, outerRadius, 0, startAngle, posAngle, true);
    g.setColour(Colors::accent());
    g.strokePath(posArc, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    float bodyR = innerRadius - 4.0f;
    g.setColour(juce::Colour(0xFF1E1E1E));
    g.fillEllipse(cx - bodyR, cy - bodyR, bodyR * 2, bodyR * 2);
    g.setColour(Colors::accent().withAlpha(0.3f));
    g.drawEllipse(cx - bodyR, cy - bodyR, bodyR * 2, bodyR * 2, 1.5f);

    float ptrR = bodyR - 4.0f;
    g.setColour(Colors::accent());
    g.drawLine(cx, cy, cx + ptrR * std::cos(posAngle), cy + ptrR * std::sin(posAngle), 2.5f);

    g.setColour(Colors::accent());
    g.fillEllipse(cx - 3, cy - 3, 6, 6);

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    juce::String valText = juce::String((int)(m_position * 100)) + "%";
    g.drawText(valText, 0, (int)cy - 6, w, 12, juce::Justification::centred);

    g.setColour(Colors::textSecondary());
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawText(m_label, 0, h - 28, w, 14, juce::Justification::centred);

    if (m_profileName.isNotEmpty()) {
        g.setColour(Colors::textMuted());
        g.setFont(juce::Font(8.0f));
        g.drawText(m_profileName, 0, h - 14, w, 14, juce::Justification::centred);
    }

    if (m_arcs.size() <= 4) {
        g.setFont(juce::Font(7.0f));
        for (int i = 0; i < (int)m_arcs.size() && i < 4; ++i) {
            g.setColour(m_arcs[i].color.withAlpha(0.8f));
            float labelAngle = startAngle + totalArc + 0.3f;
            int lx = (int)(cx + (outerRadius + 8) * std::cos(labelAngle));
            int ly = knobSize + 2 + i * 9;
            if (ly < h - 30) g.drawText(m_arcs[i].label, 2, ly, w - 4, 9, juce::Justification::left);
        }
    }
}

void MetaKnobWidget::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isLeftButtonDown()) { m_dragging = true; m_lastMousePos = e.getPosition(); }
}

void MetaKnobWidget::mouseDrag(const juce::MouseEvent& e) {
    if (m_dragging) {
        int dy = m_lastMousePos.y - e.y;
        setPosition(m_position + dy * 0.005f);
        m_lastMousePos = e.getPosition();
    }
}

void MetaKnobWidget::mouseDoubleClick(const juce::MouseEvent&) { setPosition(0.5f); }

void MetaKnobWidget::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    setPosition(m_position + (w.deltaY > 0 ? 0.01f : -0.01f));
}

} // namespace BeatMate::UI
