#include <algorithm>
#include <cmath>
#include "CamelotWheelWidget.h"
#include "../../styles/ColorPalette.h"
namespace BeatMate::UI {

namespace {
struct WheelGeom {
    float cx, cy, outerR, ringW, rB, rA, holeR;
};
WheelGeom geomFor(const juce::Component& c)
{
    WheelGeom w;
    const float side = (float)std::min(c.getWidth(), c.getHeight());
    w.cx = c.getWidth() * 0.5f;
    w.cy = c.getHeight() * 0.5f;
    w.outerR = side * 0.5f - 5.0f;
    w.ringW = w.outerR * 0.24f;
    w.rB = w.outerR - w.ringW * 0.5f;
    w.rA = w.rB - w.ringW - 4.0f;
    w.holeR = w.rA - w.ringW * 0.5f - 4.0f;
    return w;
}
bool parseKey(const juce::String& key, int& num, bool& isMinor)
{
    auto t = key.trim().toUpperCase();
    if (t.length() < 2) return false;
    auto last = t.getLastCharacter();
    if (last != 'A' && last != 'B') return false;
    num = t.dropLastCharacters(1).getIntValue();
    isMinor = (last == 'A');
    return num >= 1 && num <= 12;
}
} // namespace

CamelotWheelWidget::CamelotWheelWidget(){
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}
void CamelotWheelWidget::setHighlightedKey(const juce::String& key){
    m_highlightedKey=key;m_compatibleKeys.clear();
    if(key.isNotEmpty()){
        m_compatibleKeys.add(key);
        int num=key.dropLastCharacters(1).getIntValue();auto letter=key.getLastCharacter();
        m_compatibleKeys.add(juce::String((num%12)+1)+juce::String::charToString(letter));
        m_compatibleKeys.add(juce::String(num==1?12:num-1)+juce::String::charToString(letter));
        m_compatibleKeys.add(juce::String(num)+juce::String::charToString(letter=='A'?'B':'A'));
    }
    repaint();
}
void CamelotWheelWidget::setCompatibleKeys(const juce::StringArray& keys){m_compatibleKeys=keys;repaint();}

void CamelotWheelWidget::paint(juce::Graphics& g){
    const auto w = geomFor(*this);
    const bool hasHL = m_highlightedKey.isNotEmpty();

    for (int ring = 0; ring < 2; ++ring)
    {
        const bool isMinor = (ring == 1);
        const float r = isMinor ? w.rA : w.rB;
        const float strokeW = w.ringW - 2.0f;
        for (int i = 0; i < 12; ++i)
        {
            const juce::String name = juce::String(i + 1)
                                      + juce::String::charToString(isMinor ? 'A' : 'B');
            const juce::Colour base = Colors::camelot(i + 1, isMinor);
            const bool isHL = (name == m_highlightedKey);
            const bool isCo = !isHL && m_compatibleKeys.contains(name);
            const bool isHov = (name == m_hoveredKey);

            const float a0 = juce::degreesToRadians((float)i * 30.0f - 12.5f);
            const float a1 = juce::degreesToRadians((float)i * 30.0f + 12.5f);
            juce::Path arc;
            arc.addCentredArc(w.cx, w.cy, r, r, 0.0f, a0, a1, true);

            juce::Colour fill;
            if (isHL)       fill = base.brighter(isHov ? 0.25f : 0.10f);
            else if (isCo)  fill = base.withAlpha(isHov ? 1.0f : 0.92f);
            else            fill = base.withAlpha(hasHL ? (isHov ? 0.55f : 0.16f)
                                                        : (isHov ? 1.0f : 0.80f));

            g.setColour(fill);
            g.strokePath(arc, juce::PathStrokeType(strokeW + (isHL ? 2.0f : 0.0f),
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
            if (isHL)
            {
                juce::Path outline;
                outline.addCentredArc(w.cx, w.cy, r, r, 0.0f,
                                      a0 - 0.02f, a1 + 0.02f, true);
                g.setColour(juce::Colours::white.withAlpha(0.9f));
                g.strokePath(outline, juce::PathStrokeType(strokeW + 5.0f,
                                                           juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
                g.setColour(fill);
                g.strokePath(arc, juce::PathStrokeType(strokeW + 1.0f,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
            }

            const float mid = juce::degreesToRadians((float)i * 30.0f);
            const int tx = (int)(w.cx + r * std::sin(mid));
            const int ty = (int)(w.cy - r * std::cos(mid));
            const bool lit = isHL || isCo || !hasHL || isHov;
            const float fs = juce::jlimit(8.5f, 12.0f, w.ringW * (isMinor ? 0.42f : 0.48f));
            g.setFont(Fonts::uiFont(fs, isHL ? Fonts::Weight::Bold : Fonts::Weight::SemiBold));
            g.setColour(lit ? juce::Colours::black.withAlpha(0.78f) : Colors::textMuted());
            g.drawText(name, tx - 16, ty - 8, 32, 16, juce::Justification::centred);
        }
    }

    if (hasHL)
    {
        int num = 0; bool isMinor = false;
        const juce::Colour keyCol = parseKey(m_highlightedKey, num, isMinor)
                                        ? Colors::camelot(num, isMinor)
                                        : Colors::textPrimary();
        g.setColour(keyCol);
        g.setFont(Fonts::monoFont(juce::jmax(14.0f, w.holeR * 0.62f), Fonts::Weight::Medium));
        g.drawText(m_highlightedKey.toUpperCase(),
                   (int)(w.cx - w.holeR), (int)(w.cy - w.holeR * 0.62f),
                   (int)(w.holeR * 2.0f), (int)(w.holeR * 0.9f),
                   juce::Justification::centred);
        juce::String compat;
        for (const auto& k : m_compatibleKeys)
            if (k != m_highlightedKey)
                compat += (compat.isEmpty() ? "" : " ") + k;
        g.setColour(Colors::textSecondary());
        g.setFont(Fonts::uiFont(juce::jlimit(7.5f, 9.5f, w.holeR * 0.24f)));
        g.drawText(compat,
                   (int)(w.cx - w.holeR), (int)(w.cy + w.holeR * 0.18f),
                   (int)(w.holeR * 2.0f), (int)(w.holeR * 0.5f),
                   juce::Justification::centred);
    }
    else
    {
        g.setColour(Colors::textMuted());
        g.setFont(Fonts::uiFont(9.0f, Fonts::Weight::Medium));
        g.drawText("KEY", (int)(w.cx - w.holeR), (int)(w.cy - 8),
                   (int)(w.holeR * 2.0f), 16, juce::Justification::centred);
    }
}

void CamelotWheelWidget::mouseDown(const juce::MouseEvent& e){
    auto key=keyAtPosition(e.getPosition());
    if(key.isNotEmpty()){setHighlightedKey(key);m_listeners.call([&key](Listener&l){l.keyClicked(key);});}
}

void CamelotWheelWidget::mouseMove(const juce::MouseEvent& e){
    auto key=keyAtPosition(e.getPosition());
    if(key!=m_hoveredKey){
        m_hoveredKey=key;
        setMouseCursor(key.isNotEmpty()?juce::MouseCursor::PointingHandCursor
                                       :juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void CamelotWheelWidget::mouseExit(const juce::MouseEvent&){
    if(m_hoveredKey.isNotEmpty()){m_hoveredKey.clear();repaint();}
}

juce::String CamelotWheelWidget::keyAtPosition(juce::Point<int> pos) const{
    const auto w = geomFor(*this);
    const double dx = pos.x - w.cx, dy = pos.y - w.cy;
    const double dist = std::sqrt(dx * dx + dy * dy);
    double deg = juce::radiansToDegrees(std::atan2(dx, -dy));
    if (deg < 0) deg += 360.0;
    const int segment = ((int)((deg + 15.0) / 30.0)) % 12;
    const float half = w.ringW * 0.5f;
    if (std::abs(dist - w.rB) <= half)
        return juce::String(segment + 1) + "B";
    if (std::abs(dist - w.rA) <= half)
        return juce::String(segment + 1) + "A";
    return {};
}
} // namespace BeatMate::UI
