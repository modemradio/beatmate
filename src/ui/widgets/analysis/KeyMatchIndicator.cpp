#include "KeyMatchIndicator.h"
#include "../../styles/ColorPalette.h"
#include <cmath>
namespace BeatMate::UI {

KeyMatchIndicator::KeyMatchIndicator() { setSize(120, 28); }

void KeyMatchIndicator::setKeys(const juce::String& key1, const juce::String& key2) {
    m_key1 = key1; m_key2 = key2;
    m_matchLevel = computeMatch(key1, key2);
    repaint();
}

void KeyMatchIndicator::setMatchLevel(MatchLevel level) { m_matchLevel = level; repaint(); }

KeyMatchIndicator::MatchLevel KeyMatchIndicator::computeMatch(const juce::String& key1, const juce::String& key2) {
    if (key1.isEmpty() || key2.isEmpty()) return Unknown;
    if (key1 == key2) return Perfect;

    auto parseCamelot = [](const juce::String& key, int& num, bool& isMinor) -> bool {
        juce::String k = key.trim().toUpperCase();
        if (k.length() < 2) return false;
        char lastChar = k[k.length() - 1];
        if (lastChar != 'A' && lastChar != 'B') return false;
        isMinor = (lastChar == 'A');
        num = k.substring(0, k.length() - 1).getIntValue();
        return num >= 1 && num <= 12;
    };

    int n1 = 0, n2 = 0;
    bool minor1 = false, minor2 = false;
    if (!parseCamelot(key1, n1, minor1) || !parseCamelot(key2, n2, minor2))
        return Neutral;

    // Meme chiffre, mode different = relative majeure/mineure.
    if (n1 == n2 && minor1 != minor2) return Compatible;
    // Voisins sur la roue Camelot.
    if (minor1 == minor2) {
        int diff = std::abs(n1 - n2);
        if (diff == 1 || diff == 11) return Compatible;
    }
    if (minor1 == minor2) {
        int diff = (n2 - n1 + 12) % 12;
        if (diff == 2) return Neutral;
    }

    return Clash;
}

juce::Colour KeyMatchIndicator::getMatchColor() const {
    switch (m_matchLevel) {
        case Perfect:    return juce::Colour(0xFF00FF66);
        case Compatible: return juce::Colour(0xFF88FF00);
        case Neutral:    return juce::Colour(0xFFFFDD00);
        case Clash:      return juce::Colour(0xFFFF3333);
        case Unknown:
        default:         return Colors::textDim();
    }
}

juce::String KeyMatchIndicator::getMatchText() const {
    switch (m_matchLevel) {
        case Perfect:    return "PERFECT";
        case Compatible: return "COMPATIBLE";
        case Neutral:    return "NEUTRAL";
        case Clash:      return "CLASH";
        case Unknown:
        default:         return "---";
    }
}

void KeyMatchIndicator::paint(juce::Graphics& g) {
    int w = getWidth(), h = getHeight();
    auto matchColor = getMatchColor();

    g.setColour(Colors::bgDark());
    g.fillRoundedRectangle(0.0f, 0.0f, (float)w, (float)h, 4.0f);
    g.setColour(matchColor.withAlpha(0.08f));
    g.fillRoundedRectangle(0.0f, 0.0f, (float)w, (float)h, 4.0f);

    if (m_compact) {
        float dotSize = h - 8.0f;
        g.setColour(matchColor);
        g.fillEllipse(4.0f, 4.0f, dotSize, dotSize);
        g.setColour(matchColor.withAlpha(0.3f));
        g.fillEllipse(2.0f, 2.0f, dotSize + 4, dotSize + 4);

        g.setColour(matchColor);
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText(getMatchText(), (int)dotSize + 8, 0, w - (int)dotSize - 12, h, juce::Justification::centredLeft);
    } else {
        int third = w / 3;

        g.setColour(Colors::textSecondary());
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(m_key1, 4, 0, third - 4, h, juce::Justification::centred);

        float cx = w * 0.5f, cy = h * 0.5f;
        float indicatorR = std::min(12.0f, h * 0.35f);
        g.setColour(matchColor.withAlpha(0.25f));
        g.fillEllipse(cx - indicatorR - 3, cy - indicatorR - 3, (indicatorR + 3) * 2, (indicatorR + 3) * 2);
        g.setColour(matchColor);
        g.fillEllipse(cx - indicatorR, cy - indicatorR, indicatorR * 2, indicatorR * 2);

        if (m_showLabels) {
            g.setColour(matchColor);
            g.setFont(juce::Font(7.0f, juce::Font::bold));
            if (h > 22) {
            }
        }

        g.setColour(Colors::textSecondary());
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(m_key2, w - third, 0, third - 4, h, juce::Justification::centred);

        g.setColour(Colors::textDim());
        g.setFont(juce::Font(10.0f));
    }

    g.setColour(matchColor.withAlpha(0.3f));
    g.drawRoundedRectangle(0.5f, 0.5f, w - 1.0f, h - 1.0f, 4.0f, 1.0f);
}

} // namespace BeatMate::UI
