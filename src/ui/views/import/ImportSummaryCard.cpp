#include "ImportSummaryCard.h"
#include "../../styles/ColorPalette.h"
#include "../../../services/config/I18n.h"

namespace BeatMate::UI {

ImportSummaryCard::ImportSummaryCard()
{
    m_analyzeBtn = std::make_unique<juce::TextButton>(BM_TJ("import.summary.analyzeNow"));
    m_analyzeBtn->setColour(juce::TextButton::buttonColourId, Colors::primary().withAlpha(0.35f));
    m_analyzeBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_analyzeBtn->onClick = [this] { if (onAnalyzeNow) onAnalyzeNow(); };
    addChildComponent(*m_analyzeBtn);

    m_libraryBtn = std::make_unique<juce::TextButton>(BM_TJ("import.summary.viewLibrary"));
    m_libraryBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    m_libraryBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_libraryBtn->onClick = [this] { if (onViewLibrary) onViewLibrary(); };
    addChildComponent(*m_libraryBtn);
}

void ImportSummaryCard::showSummary(std::vector<Counter> counters, std::vector<ErrorLine> errors,
                                    bool offerAnalyze)
{
    m_counters = std::move(counters);
    m_errors = std::move(errors);
    m_visible = true;
    m_analyzeBtn->setVisible(offerAnalyze);
    m_libraryBtn->setVisible(true);
    resized();
    repaint();
}

void ImportSummaryCard::clearSummary()
{
    m_visible = false;
    m_counters.clear();
    m_errors.clear();
    m_analyzeBtn->setVisible(false);
    m_libraryBtn->setVisible(false);
    repaint();
}

void ImportSummaryCard::paint(juce::Graphics& g)
{
    if (!m_visible)
        return;

    ProDraw::glassPanel(g, getLocalBounds().toFloat(), 10.0f);

    const int w = getWidth();
    g.setColour(Colors::primary());
    g.fillRoundedRectangle(10.0f, 9.0f, 4.0f, 14.0f, 2.0f);
    g.setFont(juce::Font("Segoe UI", 12.0f, juce::Font::bold));
    g.setColour(Colors::textPrimary());
    g.drawText(BM_TJ("import.summary.title"), 20, 6, 260, 20, juce::Justification::centredLeft);

    float bx = 20.0f;
    const float by = 32.0f;
    g.setFont(juce::Font("Segoe UI", 11.0f, juce::Font::bold));
    for (const auto& c : m_counters)
    {
        const juce::String txt = juce::String(c.count) + " " + c.label;
        const float bw = juce::GlyphArrangement::getStringWidth(
                             juce::Font("Segoe UI", 11.0f, juce::Font::bold), txt) + 26.0f;
        if (bx + bw > static_cast<float>(w) - 12.0f)
            break;
        ProDraw::badge(g, txt, bx, by, bw, 22.0f, c.colour);
        bx += bw + 8.0f;
    }

    int ey = 62;
    if (!m_errors.empty())
    {
        g.setFont(juce::Font("Segoe UI", 10.0f, juce::Font::bold));
        g.setColour(Colors::error());
        g.drawText(BM_TJ("import.summary.errorsHeader"), 20, ey, w - 40, 14, juce::Justification::centredLeft);
        ey += 16;
        g.setFont(juce::Font(10.0f));
        int shown = 0;
        for (const auto& e : m_errors)
        {
            if (ey > getHeight() - 44 || shown >= 4)
            {
                const int remaining = static_cast<int>(m_errors.size()) - shown;
                if (remaining > 0)
                {
                    g.setColour(Colors::textDim());
                    g.drawText("+" + juce::String(remaining), 20, ey, w - 40, 13, juce::Justification::centredLeft);
                }
                break;
            }
            g.setColour(Colors::textSecondary());
            g.drawText(juce::File(e.file).getFileName() + "  \xe2\x80\x94  " + e.reason,
                       20, ey, w - 40, 13, juce::Justification::centredLeft, true);
            ey += 14;
            ++shown;
        }
    }
}

void ImportSummaryCard::resized()
{
    const int btnY = getHeight() - 32;
    int x = getWidth() - 12;
    if (m_libraryBtn->isVisible())
    {
        m_libraryBtn->setBounds(x - 170, btnY, 170, 24);
        x -= 178;
    }
    if (m_analyzeBtn->isVisible())
        m_analyzeBtn->setBounds(x - 170, btnY, 170, 24);
}

void ImportSummaryCard::retranslateUi()
{
    m_analyzeBtn->setButtonText(BM_TJ("import.summary.analyzeNow"));
    m_libraryBtn->setButtonText(BM_TJ("import.summary.viewLibrary"));
    repaint();
}

}
