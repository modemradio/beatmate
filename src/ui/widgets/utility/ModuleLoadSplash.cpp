#include "ModuleLoadSplash.h"
#include "../../styles/ColorPalette.h"
#include "BinaryData.h"
#include <cmath>

namespace BeatMate::UI::Widgets {

namespace {
constexpr int kTimerHz = 30;
constexpr float kFadeInMs = 200.0f;
constexpr float kFadeOutMs = 250.0f;
}

ModuleLoadSplash::ModuleLoadSplash(const juce::String& moduleTitle,
                                   const juce::String& moduleSubtitle,
                                   std::vector<juce::String> stages)
    : m_title(moduleTitle), m_subtitle(moduleSubtitle), m_stages(std::move(stages))
{
    setOpaque(true);
    setInterceptsMouseClicks(true, false);
    m_startTime = std::chrono::steady_clock::now();
    startTimerHz(kTimerHz);
}

ModuleLoadSplash::~ModuleLoadSplash() { stopTimer(); }

void ModuleLoadSplash::setStageIndex(int idx) {
    m_stageIndex.store(juce::jlimit(0, (int)m_stages.size() - 1, idx));
    m_stageProgress.store(0.0f);
}

void ModuleLoadSplash::setStageProgress(float p) {
    m_stageProgress.store(juce::jlimit(0.0f, 1.0f, p));
}

void ModuleLoadSplash::complete() {
    m_completed.store(true);
    m_fadingOut = true;
}

void ModuleLoadSplash::timerCallback() {
    const float dtMs = 1000.0f / (float)kTimerHz;
    m_phase += dtMs / 1000.0f;

    if (!m_fadingOut) {
        m_alpha = juce::jmin(1.0f, m_alpha + dtMs / kFadeInMs);
    } else {
        m_fadeOutAlpha -= dtMs / kFadeOutMs;
        if (m_fadeOutAlpha <= 0.0f) {
            m_fadeOutAlpha = 0.0f;
            stopTimer();
            if (onCompleted) {
                auto cb = std::move(onCompleted);
                juce::MessageManager::callAsync([cb]() { if (cb) cb(); });
            }
        }
    }
    repaint();
}

void ModuleLoadSplash::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    const float globalAlpha = m_alpha * m_fadeOutAlpha;
    const float h = bounds.getHeight();
    const float w = bounds.getWidth();

    {
        juce::ColourGradient bg(
            juce::Colour(0xFF0F1419), bounds.getX(), bounds.getY(),
            juce::Colour(0xFF050810), bounds.getRight(), bounds.getBottom(),
            false);
        g.setGradientFill(bg);
        g.fillAll();
    }

    {
        auto cx = bounds.getCentreX();
        auto cy = bounds.getY() + h * 0.30f;
        const float r = h * 0.40f;
        juce::ColourGradient halo(
            Colors::primary().withAlpha(0.10f * globalAlpha), cx, cy,
            Colors::primary().withAlpha(0.0f), cx + r, cy + r,
            true);
        g.setGradientFill(halo);
        g.fillEllipse(cx - r, cy - r, r * 2, r * 2);
    }

    const float logoH = h * 0.20f;
    const float logoW = w * 0.46f;
    juce::Rectangle<float> logoR(bounds.getCentreX() - logoW * 0.5f,
                                  bounds.getY() + h * 0.07f,
                                  logoW, logoH);
    paintLogo(g, logoR);

    {
        auto titleY = logoR.getBottom() + h * 0.04f;
        auto titleH = h * 0.09f;
        g.setColour(Colors::textPrimary().withAlpha(globalAlpha));
        g.setFont(juce::FontOptions("Segoe UI", h * 0.065f, juce::Font::bold));
        juce::Rectangle<float> rc(0, titleY, w, titleH);
        g.drawText(m_title, rc, juce::Justification::centred, false);
    }

    {
        auto subY = bounds.getY() + h * 0.36f;
        g.setColour(Colors::textMuted().withAlpha(globalAlpha));
        g.setFont(juce::FontOptions("Segoe UI", h * 0.020f, juce::Font::plain));
        juce::Rectangle<float> rc(0, subY, w, h * 0.04f);
        g.drawText(m_subtitle.toUpperCase(), rc, juce::Justification::centred, false);
    }

    {
        const float dy = bounds.getY() + h * 0.43f;
        const float dw = w * 0.35f;
        const float dx = (w - dw) * 0.5f;
        g.setColour(Colors::border().withAlpha(0.7f * globalAlpha));
        g.fillRect(juce::Rectangle<float>(dx, dy, dw, 1.0f));
    }

    const int idx = m_stageIndex.load();
    const float subP = m_stageProgress.load();
    const float globalPct = m_stages.empty() ? 1.0f
        : (float)idx / (float)m_stages.size() + subP / (float)m_stages.size();

    if (!m_stages.empty()) {
        const int curIdx = juce::jlimit(0, (int)m_stages.size() - 1, idx);
        const juce::String currentStage = m_stages[(size_t)curIdx];

        const float tagY = bounds.getY() + h * 0.48f;
        g.setColour(Colors::primary().withAlpha(globalAlpha));
        g.setFont(juce::FontOptions("Segoe UI", h * 0.018f, juce::Font::bold));
        g.drawText("EN COURS DE CHARGEMENT",
                   juce::Rectangle<float>(0, tagY, w, h * 0.025f),
                   juce::Justification::centred, false);

        const float stageY = tagY + h * 0.03f;
        g.setColour(Colors::textPrimary().withAlpha(globalAlpha));
        g.setFont(juce::FontOptions("Segoe UI", h * 0.030f, juce::Font::bold));
        g.drawText(currentStage,
                   juce::Rectangle<float>(0, stageY, w, h * 0.045f),
                   juce::Justification::centred, false);

        const float stepY = stageY + h * 0.045f;
        g.setColour(Colors::textMuted().withAlpha(globalAlpha));
        g.setFont(juce::FontOptions("Segoe UI", h * 0.015f, juce::Font::plain));
        g.drawText(juce::String::fromUTF8("\xC3\x89tape ") + juce::String(curIdx + 1)
                   + juce::String::fromUTF8(" sur ") + juce::String((int)m_stages.size()),
                   juce::Rectangle<float>(0, stepY, w, h * 0.022f),
                   juce::Justification::centred, false);
    }

    {
        const int barW = (int)(w * 0.60f);
        const int barH = 4;
        juce::Rectangle<int> barR((int)((w - barW) * 0.5f),
                                    (int)(bounds.getY() + h * 0.66f),
                                    barW, barH);
        paintProgressBar(g, barR, globalPct);
    }

    {
        const int pctInt = (int)(globalPct * 100.0f);
        g.setColour(Colors::textSecondary().withAlpha(globalAlpha));
        g.setFont(juce::FontOptions("Segoe UI", h * 0.020f, juce::Font::plain));
        g.drawText(juce::String(pctInt) + "%",
                   juce::Rectangle<float>(0, bounds.getY() + h * 0.69f, w, h * 0.030f),
                   juce::Justification::centred, false);
    }

    if (!m_stages.empty()) {
        const float listY = bounds.getY() + h * 0.74f;
        const float lineH = h * 0.030f;
        const float cardW = w * 0.55f;
        const float cardX = (w - cardW) * 0.5f;
        const float cardY = listY - 8.0f;
        const float cardH = lineH * (float)m_stages.size() + 14.0f;

        g.setColour(juce::Colour(0xFF080B11).withAlpha(0.7f * globalAlpha));
        g.fillRoundedRectangle(cardX, cardY, cardW, cardH, 4.0f);
        g.setColour(Colors::border().withAlpha(0.5f * globalAlpha));
        g.drawRoundedRectangle(cardX, cardY, cardW, cardH, 4.0f, 1.0f);

        g.setFont(juce::FontOptions("Segoe UI", h * 0.016f, juce::Font::plain));

        for (int i = 0; i < (int)m_stages.size(); ++i) {
            const float yLine = listY + i * lineH;
            juce::String iconStr;
            juce::Colour iconCol, textCol;

            if (i < idx) {
                iconStr = juce::String::fromUTF8("\xE2\x9C\x93");
                iconCol = Colors::success().withAlpha(globalAlpha);
                textCol = Colors::textSecondary().withAlpha(globalAlpha * 0.85f);
            } else if (i == idx) {
                iconStr = juce::String::fromUTF8("\xE2\x96\xB6");
                iconCol = Colors::primary().withAlpha(globalAlpha);
                textCol = Colors::textPrimary().withAlpha(globalAlpha);
            } else {
                iconStr = juce::String::fromUTF8("\xE2\x97\x8B");
                iconCol = Colors::textMuted().withAlpha(globalAlpha * 0.5f);
                textCol = Colors::textMuted().withAlpha(globalAlpha * 0.5f);
            }

            const float iconX = cardX + 16;
            const float textX = cardX + 36;
            g.setColour(iconCol);
            g.setFont(juce::FontOptions("Segoe UI", h * 0.018f, juce::Font::bold));
            g.drawText(iconStr,
                       juce::Rectangle<float>(iconX, yLine, 16, lineH),
                       juce::Justification::centredLeft, false);

            g.setColour(textCol);
            g.setFont(juce::FontOptions("Segoe UI", h * 0.016f, juce::Font::plain));
            g.drawText(m_stages[(size_t)i],
                       juce::Rectangle<float>(textX, yLine, cardW - (textX - cardX) - 16, lineH),
                       juce::Justification::centredLeft, false);
        }
    }

    {
        g.setColour(Colors::textMuted().withAlpha(globalAlpha * 0.6f));
        g.setFont(juce::FontOptions("Segoe UI", h * 0.014f, juce::Font::plain));
        juce::Rectangle<float> rc(0, bounds.getBottom() - h * 0.035f, w, h * 0.025f);
        g.drawText(juce::String::fromUTF8("BeatMate V12.0.0   \xC2\xB7   Professional DJ Suite"),
                   rc, juce::Justification::centred, false);
    }
}

void ModuleLoadSplash::paintLogo(juce::Graphics& g, juce::Rectangle<float> r) {
    const float globalAlpha = m_alpha * m_fadeOutAlpha;

    static const juce::Image logo = juce::ImageCache::getFromMemory(
        BinaryData::beatmate_logo_full_png, BinaryData::beatmate_logo_full_pngSize);

    if (logo.isValid()) {
        g.setOpacity(globalAlpha);
        g.drawImage(logo, r, juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
        g.setOpacity(1.0f);
        return;
    }

    const float radius = r.getHeight() * 0.18f;
    juce::ColourGradient grad(
        Colors::primary().withAlpha(globalAlpha), r.getX(), r.getY(),
        Colors::secondary().withAlpha(globalAlpha), r.getRight(), r.getBottom(),
        false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(r, radius);

    g.setColour(juce::Colours::white.withAlpha(globalAlpha));
    g.setFont(juce::FontOptions("Segoe UI", r.getHeight() * 0.55f, juce::Font::bold));
    g.drawText("BM", r, juce::Justification::centred, false);
}

void ModuleLoadSplash::paintProgressBar(juce::Graphics& g, juce::Rectangle<int> r, float pct) {
    const float globalAlpha = m_alpha * m_fadeOutAlpha;
    auto rf = r.toFloat();
    const float radius = rf.getHeight() * 0.5f;

    g.setColour(Colors::border().withAlpha(globalAlpha * 0.7f));
    g.fillRoundedRectangle(rf, radius);

    const float fillW = rf.getWidth() * juce::jlimit(0.0f, 1.0f, pct);
    if (fillW < 1.0f) return;

    auto fillR = rf.withWidth(fillW);
    juce::ColourGradient grad(
        Colors::primary().withAlpha(globalAlpha), fillR.getX(), fillR.getY(),
        Colors::secondary().withAlpha(globalAlpha), fillR.getRight(), fillR.getY(),
        false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(fillR, radius);
}

void ModuleLoadSplash::resized() { /* full bounds layout in paint() */ }

}
