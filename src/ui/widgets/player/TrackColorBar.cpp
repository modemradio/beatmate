#include "TrackColorBar.h"
#include "../../styles/ColorPalette.h"
namespace BeatMate::UI {

TrackColorBar::TrackColorBar() { setSize(200, 6); }

void TrackColorBar::setColor(const juce::Colour& color) {
    m_color = color;
    m_listeners.call([&color](Listener& l) { l.colorChanged(color); });
    repaint();
}

std::vector<juce::Colour> TrackColorBar::getPresetColors() {
    return {
        juce::Colour(0xFFFF0000), juce::Colour(0xFFFF8800), juce::Colour(0xFFFFFF00),
        juce::Colour(0xFF00FF00), juce::Colour(0xFF00FFFF), juce::Colour(0xFF0088FF),
        juce::Colour(0xFF8800FF), juce::Colour(0xFFFF00FF), juce::Colour(0xFFFF6B9D),
        juce::Colour(0xFF00FFA3), juce::Colour(0xFFFFD700), juce::Colour(0xFF0078D4),
        juce::Colour(0xFFFF3838), juce::Colour(0xFFB048FF), juce::Colour(0xFF999999),
        juce::Colour(0xFFFFFFFF)
    };
}

void TrackColorBar::paint(juce::Graphics& g) {
    int w = getWidth(), h = getHeight();

    if (m_horizontal) {
        if (m_glow) {
            juce::ColourGradient glow(m_color.withAlpha(0.3f), 0, 0, m_color.withAlpha(0.0f), 0, (float)std::min(h, m_thickness * 3), false);
            g.setGradientFill(glow);
            g.fillRect(0, 0, w, std::min(h, m_thickness * 3));
        }
        g.setColour(m_color);
        g.fillRect(0, 0, w, m_thickness);
    } else {
        if (m_glow) {
            juce::ColourGradient glow(m_color.withAlpha(0.3f), 0, 0, m_color.withAlpha(0.0f), (float)std::min(w, m_thickness * 3), 0, false);
            g.setGradientFill(glow);
            g.fillRect(0, 0, std::min(w, m_thickness * 3), h);
        }
        g.setColour(m_color);
        g.fillRect(0, 0, m_thickness, h);
    }

    if (m_showTitle && m_title.isNotEmpty()) {
        g.setColour(m_color.contrasting(0.8f));
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        if (m_horizontal) {
            g.drawText(m_title, 4, 0, w - 8, std::max(h, 14), juce::Justification::centredLeft);
        } else {
            g.drawText(m_title, m_thickness + 2, 4, w - m_thickness - 4, 14, juce::Justification::centredLeft);
        }
    }
}

void TrackColorBar::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isRightButtonDown() || e.mods.isPopupMenu()) {
        auto* popup = new juce::PopupMenu();
        auto colors = getPresetColors();
        for (int i = 0; i < (int)colors.size(); ++i) {
            popup->addItem(i + 1, "Color " + juce::String(i + 1));
        }
        int result = popup->show();
        if (result > 0 && result <= (int)colors.size()) {
            setColor(colors[result - 1]);
        }
        delete popup;
    }
}

} // namespace BeatMate::UI
