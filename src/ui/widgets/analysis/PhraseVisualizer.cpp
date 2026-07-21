#include "PhraseVisualizer.h"
#include "../../styles/ColorPalette.h"

namespace BeatMate::UI {

PhraseVisualizer::PhraseVisualizer() { setSize(400, 40); }

void PhraseVisualizer::setPhrases(const std::vector<PhraseSegment>& phrases)
{
    m_phrases = phrases;
    for (auto& p : m_phrases)
    {
        if (p.color == juce::Colour())
            p.color = getDefaultColor(p.label);
    }
    repaint();
}

juce::Colour PhraseVisualizer::getDefaultColor(const juce::String& label) const
{
    juce::String l = label.toLowerCase();
    if (l.contains("intro"))   return juce::Colour(0xFF4A90E2);
    if (l.contains("verse"))   return juce::Colour(0xFF00CC88);
    if (l.contains("chorus"))  return juce::Colour(0xFFFF6B9D);
    if (l.contains("drop"))    return juce::Colour(0xFFFF3838);
    if (l.contains("break"))   return juce::Colour(0xFFFFD700);
    if (l.contains("build"))   return juce::Colour(0xFFFF8800);
    if (l.contains("outro"))   return juce::Colour(0xFFB048FF);
    if (l.contains("bridge"))  return juce::Colour(0xFF00D9FF);
    return juce::Colour(0xFF666666);
}

void PhraseVisualizer::paint(juce::Graphics& g)
{
    int w = getWidth(), h = getHeight();
    g.fillAll(Colors::bgDarkest());

    if (m_totalDuration <= 0 || m_phrases.empty())
    {
        g.setColour(Colors::textDim());
        g.setFont(juce::Font(10.0f));
        g.drawText("No phrase data", 0, 0, w, h, juce::Justification::centred);
        return;
    }

    int labelH = m_showLabels ? 16 : 0;
    int barY = labelH;
    int barH = h - labelH - (m_showEnergy ? 14 : 0);
    int energyY = barY + barH + 2;
    int energyH = m_showEnergy ? 12 : 0;

    for (int i = 0; i < (int)m_phrases.size(); ++i)
    {
        auto& p = m_phrases[i];
        float x1 = (float)(p.startSeconds / m_totalDuration * w);
        float x2 = (float)(p.endSeconds / m_totalDuration * w);
        float segW = std::max(2.0f, x2 - x1);

        {
            juce::ColourGradient segGrad(
                p.color.withAlpha(0.75f), x1, (float)barY,
                p.color.withAlpha(0.45f), x1, (float)(barY + barH), false);
            g.setGradientFill(segGrad);
            g.fillRect(x1, (float)barY, segW, (float)barH);
        }

        {
            juce::ColourGradient topHL(
                juce::Colours::white.withAlpha(0.12f), x1, (float)barY,
                juce::Colours::transparentWhite, x1, (float)(barY + barH * 0.35f), false);
            g.setGradientFill(topHL);
            g.fillRect(x1, (float)barY, segW, (float)(barH * 0.35f));
        }

        g.setColour(Colors::bgDarkest().withAlpha(0.8f));
        g.fillRect(x1, (float)barY, 1.0f, (float)barH);

        if (m_showEnergy && energyH > 0)
        {
            float eH = p.energy * energyH;
            if (eH > 0.5f)
            {
                juce::Colour eCol;
                if (p.energy < 0.4f)
                    eCol = Colors::energyLow().interpolatedWith(Colors::energyMedium(), p.energy / 0.4f);
                else if (p.energy < 0.7f)
                    eCol = Colors::energyMedium().interpolatedWith(Colors::energyHigh(), (p.energy - 0.4f) / 0.3f);
                else
                    eCol = Colors::energyHigh();

                juce::ColourGradient eGrad(
                    eCol.withAlpha(0.7f), x1, (float)(energyY + energyH) - eH,
                    eCol.withAlpha(0.3f), x1, (float)(energyY + energyH), false);
                g.setGradientFill(eGrad);
                g.fillRect(x1, (float)(energyY + energyH) - eH, segW, eH);
            }
        }

        if (m_showLabels && segW > 22)
        {
            juce::String lbl = p.label;
            if (lbl.isEmpty())
                lbl = "P" + juce::String(p.phraseNumber + 1);

            g.setFont(juce::Font(8.5f, juce::Font::bold));
            float textW = g.getCurrentFont().getStringWidthFloat(lbl) + 6.0f;
            float textH = 11.0f;
            float textX = x1 + 3.0f;
            float textY = 1.0f;

            g.setColour(Colors::bgDarkest().withAlpha(0.7f));
            g.fillRoundedRectangle(textX, textY, std::min(textW, segW - 6.0f), textH, 3.0f);

            g.setColour(p.color.withAlpha(0.4f));
            g.drawRoundedRectangle(textX, textY, std::min(textW, segW - 6.0f), textH, 3.0f, 0.5f);

            g.setColour(p.color.brighter(0.5f));
            g.drawText(lbl, (int)(textX + 3), (int)textY, (int)(segW - 10), (int)textH,
                       juce::Justification::centredLeft);
        }
    }

    if (m_currentPosition >= 0 && m_currentPosition <= m_totalDuration)
    {
        float posX = (float)(m_currentPosition / m_totalDuration * w);

        juce::ColourGradient cursorGlow(
            juce::Colours::white.withAlpha(0.2f), posX, (float)barY,
            juce::Colours::transparentWhite, posX + 6.0f, (float)barY, true);
        g.setGradientFill(cursorGlow);
        g.fillRect(posX - 6.0f, (float)barY, 12.0f, (float)barH);

        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.fillRect(posX - 0.5f, (float)barY, 2.0f, (float)barH);

        juce::Path tri;
        tri.addTriangle(posX - 4, (float)barY, posX + 4, (float)barY, posX, (float)(barY + 5));
        g.setColour(juce::Colours::white);
        g.fillPath(tri);
    }

    g.setColour(Colors::border());
    g.drawRect(0, barY, w, barH, 1);
}

void PhraseVisualizer::mouseDown(const juce::MouseEvent& e)
{
    if (m_totalDuration <= 0) return;
    double clickPos = (double)e.x / getWidth() * m_totalDuration;
    for (int i = 0; i < (int)m_phrases.size(); ++i)
    {
        if (clickPos >= m_phrases[i].startSeconds && clickPos < m_phrases[i].endSeconds)
        {
            m_listeners.call([i, clickPos](Listener& l) { l.phraseClicked(i, clickPos); });
            break;
        }
    }
}

} // namespace BeatMate::UI
