#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

#include "ColorPalette.h"


namespace BeatMate::UI::Icons {

inline juce::Path play()
{
    juce::Path p;
    p.startNewSubPath(2.0f, 1.0f);
    p.lineTo(2.0f, 15.0f);
    p.lineTo(14.0f, 8.0f);
    p.closeSubPath();
    return p;
}

inline juce::Path stop()
{
    juce::Path p;
    p.addRoundedRectangle(2.0f, 2.0f, 12.0f, 12.0f, 1.0f);
    return p;
}

inline juce::Path pause()
{
    juce::Path p;
    p.addRoundedRectangle(3.0f, 2.0f, 3.5f, 12.0f, 0.6f);
    p.addRoundedRectangle(9.5f, 2.0f, 3.5f, 12.0f, 0.6f);
    return p;
}

inline juce::Path skipNext()
{
    juce::Path p;
    p.startNewSubPath(2.0f, 2.0f);
    p.lineTo(2.0f, 14.0f);
    p.lineTo(11.0f, 8.0f);
    p.closeSubPath();
    p.addRectangle(12.0f, 2.0f, 2.0f, 12.0f);
    return p;
}

inline juce::Path skipPrev()
{
    juce::Path p;
    p.addRectangle(2.0f, 2.0f, 2.0f, 12.0f);
    p.startNewSubPath(14.0f, 2.0f);
    p.lineTo(14.0f, 14.0f);
    p.lineTo(5.0f, 8.0f);
    p.closeSubPath();
    return p;
}

inline juce::Path record()
{
    juce::Path p;
    p.addEllipse(2.5f, 2.5f, 11.0f, 11.0f);
    return p;
}

inline juce::Path replay()
{
    juce::Path p;
    p.addCentredArc(8.0f, 8.5f, 5.5f, 5.5f,
                    0.0f,
                    juce::MathConstants<float>::pi * 0.25f,
                    juce::MathConstants<float>::pi * 1.95f,
                    true);
    p.startNewSubPath(2.5f, 4.0f);
    p.lineTo(4.5f, 1.5f);
    p.lineTo(7.0f, 3.5f);
    return p;
}

inline juce::Path mute()
{
    juce::Path p;
    p.startNewSubPath(2.0f, 6.0f);
    p.lineTo(5.0f, 6.0f);
    p.lineTo(8.5f, 3.0f);
    p.lineTo(8.5f, 13.0f);
    p.lineTo(5.0f, 10.0f);
    p.lineTo(2.0f, 10.0f);
    p.closeSubPath();
    p.startNewSubPath(11.0f, 5.5f);
    p.lineTo(15.0f, 10.5f);
    p.startNewSubPath(15.0f, 5.5f);
    p.lineTo(11.0f, 10.5f);
    return p;
}

inline juce::Path solo()
{
    juce::Path p;
    p.addEllipse(1.0f, 1.0f, 14.0f, 14.0f);
    juce::Path s;
    s.startNewSubPath(11.0f, 5.5f);
    s.cubicTo(10.0f, 3.5f, 6.0f, 3.5f, 5.5f, 6.0f);
    s.cubicTo(5.0f, 8.5f, 11.0f, 7.5f, 10.5f, 10.0f);
    s.cubicTo(10.0f, 12.5f, 6.0f, 12.5f, 5.0f, 10.5f);
    p.addPath(s);
    return p;
}

inline juce::Path automation()
{
    juce::Path p;
    p.startNewSubPath(2.0f, 14.0f);
    p.lineTo(8.0f, 2.0f);
    p.lineTo(14.0f, 14.0f);
    p.startNewSubPath(4.5f, 9.0f);
    p.lineTo(11.5f, 9.0f);
    return p;
}

inline juce::Path lock()
{
    juce::Path p;
    p.addRoundedRectangle(3.0f, 7.0f, 10.0f, 8.0f, 1.0f);
    juce::Path arc;
    arc.addCentredArc(8.0f, 7.0f, 3.0f, 3.0f,
                      0.0f,
                      -juce::MathConstants<float>::halfPi,
                      juce::MathConstants<float>::halfPi,
                      true);
    arc.lineTo(11.0f, 7.0f);
    p.addPath(arc);
    return p;
}

inline juce::Path scissors()
{
    juce::Path p;
    p.addEllipse(1.5f, 9.5f, 4.0f, 4.0f);
    p.addEllipse(1.5f, 2.5f, 4.0f, 4.0f);
    p.startNewSubPath(5.0f, 4.5f);
    p.lineTo(14.5f, 11.5f);
    p.startNewSubPath(5.0f, 11.5f);
    p.lineTo(14.5f, 4.5f);
    return p;
}

inline juce::Path zoomIn()
{
    juce::Path p;
    p.addEllipse(1.5f, 1.5f, 10.0f, 10.0f);
    p.startNewSubPath(6.5f, 4.0f);
    p.lineTo(6.5f, 9.0f);
    p.startNewSubPath(4.0f, 6.5f);
    p.lineTo(9.0f, 6.5f);
    p.startNewSubPath(10.0f, 10.0f);
    p.lineTo(15.0f, 15.0f);
    return p;
}

inline juce::Path zoomOut()
{
    juce::Path p;
    p.addEllipse(1.5f, 1.5f, 10.0f, 10.0f);
    p.startNewSubPath(4.0f, 6.5f);
    p.lineTo(9.0f, 6.5f);
    p.startNewSubPath(10.0f, 10.0f);
    p.lineTo(15.0f, 15.0f);
    return p;
}

inline juce::Path fitView()
{
    juce::Path p;
    p.startNewSubPath(2.0f, 5.0f);
    p.lineTo(2.0f, 2.0f);
    p.lineTo(5.0f, 2.0f);

    p.startNewSubPath(11.0f, 2.0f);
    p.lineTo(14.0f, 2.0f);
    p.lineTo(14.0f, 5.0f);

    p.startNewSubPath(14.0f, 11.0f);
    p.lineTo(14.0f, 14.0f);
    p.lineTo(11.0f, 14.0f);

    p.startNewSubPath(5.0f, 14.0f);
    p.lineTo(2.0f, 14.0f);
    p.lineTo(2.0f, 11.0f);
    return p;
}

inline juce::Path centerPlayhead()
{
    juce::Path p;
    p.addRectangle(7.5f, 2.0f, 1.0f, 12.0f);
    p.startNewSubPath(8.0f, 0.5f);
    p.lineTo(11.0f, 3.0f);
    p.lineTo(8.0f, 5.5f);
    p.lineTo(5.0f, 3.0f);
    p.closeSubPath();
    return p;
}

inline juce::Path undo()
{
    juce::Path p;
    p.addCentredArc(8.0f, 9.0f, 5.0f, 4.0f,
                    0.0f,
                    -juce::MathConstants<float>::halfPi,
                    juce::MathConstants<float>::pi,
                    true);
    p.startNewSubPath(2.5f, 4.5f);
    p.lineTo(3.0f, 9.0f);
    p.lineTo(7.5f, 8.5f);
    return p;
}

inline juce::Path redo()
{
    juce::Path p;
    p.addCentredArc(8.0f, 9.0f, 5.0f, 4.0f,
                    0.0f,
                    juce::MathConstants<float>::halfPi,
                    -juce::MathConstants<float>::pi,
                    true);
    p.startNewSubPath(13.5f, 4.5f);
    p.lineTo(13.0f, 9.0f);
    p.lineTo(8.5f, 8.5f);
    return p;
}

inline juce::Path save()
{
    juce::Path p;
    p.addRoundedRectangle(2.0f, 2.0f, 12.0f, 12.0f, 1.0f);
    p.addRectangle(5.0f, 2.0f, 6.0f, 4.0f);
    p.addRectangle(4.0f, 9.0f, 8.0f, 5.0f);
    return p;
}

inline juce::Path open()
{
    juce::Path p;
    p.startNewSubPath(2.0f, 5.0f);
    p.lineTo(2.0f, 13.0f);
    p.lineTo(14.0f, 13.0f);
    p.lineTo(14.0f, 6.0f);
    p.lineTo(8.0f, 6.0f);
    p.lineTo(6.5f, 4.5f);
    p.lineTo(2.0f, 4.5f);
    p.closeSubPath();
    return p;
}

inline juce::Path xnew()
{
    juce::Path p;
    p.startNewSubPath(3.0f, 1.5f);
    p.lineTo(10.5f, 1.5f);
    p.lineTo(13.5f, 4.5f);
    p.lineTo(13.5f, 14.5f);
    p.lineTo(3.0f, 14.5f);
    p.closeSubPath();
    p.startNewSubPath(10.5f, 1.5f);
    p.lineTo(10.5f, 4.5f);
    p.lineTo(13.5f, 4.5f);
    return p;
}

inline juce::Path settings()
{
    juce::Path p;
    constexpr int teeth = 8;
    const float cx = 8.0f, cy = 8.0f;
    const float rOut = 7.0f, rIn = 5.0f;
    for (int i = 0; i < teeth; ++i)
    {
        float a = (i / (float)teeth) * juce::MathConstants<float>::twoPi;
        float a2 = a + juce::MathConstants<float>::twoPi / (teeth * 2);
        float x1 = cx + std::cos(a) * rOut;
        float y1 = cy + std::sin(a) * rOut;
        float x2 = cx + std::cos(a2) * rIn;
        float y2 = cy + std::sin(a2) * rIn;
        if (i == 0) p.startNewSubPath(x1, y1);
        else        p.lineTo(x1, y1);
        p.lineTo(x2, y2);
    }
    p.closeSubPath();
    p.addEllipse(cx - 2.0f, cy - 2.0f, 4.0f, 4.0f);
    return p;
}

inline juce::Path close()
{
    juce::Path p;
    p.startNewSubPath(3.0f, 3.0f);
    p.lineTo(13.0f, 13.0f);
    p.startNewSubPath(13.0f, 3.0f);
    p.lineTo(3.0f, 13.0f);
    return p;
}

inline juce::Path metronome()
{
    juce::Path p;
    p.startNewSubPath(5.0f, 2.0f);
    p.lineTo(11.0f, 2.0f);
    p.lineTo(13.5f, 14.5f);
    p.lineTo(2.5f, 14.5f);
    p.closeSubPath();
    p.startNewSubPath(8.0f, 13.5f);
    p.lineTo(12.0f, 4.5f);
    return p;
}

inline juce::Path waveform()
{
    juce::Path p;
    static const float heights[] = { 4.0f, 8.0f, 5.0f, 11.0f, 7.0f, 13.0f, 6.0f, 9.0f, 4.0f };
    const int n = (int)(sizeof(heights) / sizeof(heights[0]));
    const float spacing = 14.0f / n;
    for (int i = 0; i < n; ++i)
    {
        float h = heights[i];
        float x = 1.0f + i * spacing;
        float y = 8.0f - h * 0.5f;
        p.addRoundedRectangle(x, y, spacing * 0.6f, h, 0.4f);
    }
    return p;
}

inline juce::Path stems()
{
    juce::Path p;
    for (int i = 0; i < 4; ++i)
    {
        float y = 1.5f + i * 3.5f;
        p.addRoundedRectangle(1.5f, y, 13.0f, 2.4f, 0.6f);
    }
    return p;
}

inline juce::Path harmonize()
{
    juce::Path p;
    p.addEllipse(2.0f, 10.5f, 4.0f, 3.5f);
    p.addEllipse(9.0f, 8.0f, 4.0f, 3.5f);
    p.startNewSubPath(6.0f, 12.5f);
    p.lineTo(6.0f, 3.0f);
    p.lineTo(13.0f, 1.5f);
    p.lineTo(13.0f, 9.5f);
    return p;
}

inline juce::Path plus()
{
    juce::Path p;
    p.addRoundedRectangle(7.0f, 2.0f, 2.0f, 12.0f, 0.6f);
    p.addRoundedRectangle(2.0f, 7.0f, 12.0f, 2.0f, 0.6f);
    return p;
}

inline juce::Path sparkleAI()
{
    juce::Path p;
    p.startNewSubPath(8.0f, 1.0f);
    p.lineTo(9.5f, 6.5f);
    p.lineTo(15.0f, 8.0f);
    p.lineTo(9.5f, 9.5f);
    p.lineTo(8.0f, 15.0f);
    p.lineTo(6.5f, 9.5f);
    p.lineTo(1.0f, 8.0f);
    p.lineTo(6.5f, 6.5f);
    p.closeSubPath();
    return p;
}

} // namespace BeatMate::UI::Icons


namespace BeatMate::UI {

class IconButton : public juce::Button
{
public:
    IconButton(const juce::String& name,
               juce::Path icon,
               juce::Colour color = Colors::textPrimary())
        : juce::Button(name)
        , m_icon(std::move(icon))
        , m_color(color)
    {
    }

    void setIcon(juce::Path icon)        { m_icon = std::move(icon); repaint(); }
    void setIconColour(juce::Colour c)   { m_color = c; repaint(); }
    void setFilled(bool fill)            { m_filled = fill; repaint(); }

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        auto bounds = getLocalBounds().toFloat();

        auto bg = juce::Colour(0xFF1A1A24);
        if (getToggleState())
            bg = m_color.withAlpha(0.4f);
        else if (down)
            bg = bg.brighter(0.10f);
        else if (over)
            bg = bg.brighter(0.05f);

        g.setColour(bg);
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(juce::Colour(0xFF2A2A38));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

        auto iconArea = bounds.reduced(juce::jmax(3.0f, bounds.getHeight() * 0.20f));
        if (m_icon.isEmpty()) return;

        auto srcBounds = m_icon.getBounds();
        if (srcBounds.isEmpty()) return;

        auto t = juce::AffineTransform();
        t = t.translated(-srcBounds.getX(), -srcBounds.getY());
        const float scale = juce::jmin(iconArea.getWidth() / srcBounds.getWidth(),
                                        iconArea.getHeight() / srcBounds.getHeight());
        t = t.scaled(scale);
        const float dx = iconArea.getX() + (iconArea.getWidth() - srcBounds.getWidth() * scale) * 0.5f;
        const float dy = iconArea.getY() + (iconArea.getHeight() - srcBounds.getHeight() * scale) * 0.5f;
        t = t.translated(dx, dy);

        auto col = getToggleState() ? juce::Colours::white : m_color;
        g.setColour(col);
        if (m_filled)
            g.fillPath(m_icon, t);
        else
            g.strokePath(m_icon, juce::PathStrokeType(1.4f,
                                                        juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded), t);
    }

private:
    juce::Path  m_icon;
    juce::Colour m_color;
    bool        m_filled { true };
};

class IconTextButton : public juce::TextButton
{
public:
    explicit IconTextButton(const juce::String& label = {}) : juce::TextButton(label) {}

    void setLeadingIcon(juce::Path icon, juce::Colour tint = juce::Colours::white, bool filled = true)
    {
        m_icon = std::move(icon);
        m_iconColor = tint;
        m_iconFilled = filled;
        repaint();
    }

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        juce::TextButton::paintButton(g, over, down);
        if (m_icon.isEmpty()) return;

        auto bounds = getLocalBounds().toFloat();
        const float pad = 4.0f;
        const float size = juce::jmin(bounds.getHeight() - pad * 2.0f, 14.0f);
        juce::Rectangle<float> iconArea(pad, (bounds.getHeight() - size) * 0.5f, size, size);

        auto srcBounds = m_icon.getBounds();
        if (srcBounds.isEmpty()) return;

        auto t = juce::AffineTransform()
                    .translated(-srcBounds.getX(), -srcBounds.getY())
                    .scaled(juce::jmin(iconArea.getWidth() / srcBounds.getWidth(),
                                        iconArea.getHeight() / srcBounds.getHeight()));
        t = t.translated(iconArea.getX(), iconArea.getY());

        g.setColour(m_iconColor);
        if (m_iconFilled)
            g.fillPath(m_icon, t);
        else
            g.strokePath(m_icon, juce::PathStrokeType(1.3f,
                                                        juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded), t);
    }

private:
    juce::Path   m_icon;
    juce::Colour m_iconColor { juce::Colours::white };
    bool         m_iconFilled { true };
};

} // namespace BeatMate::UI
