#include "AnalysisProgressCard.h"
#include "../../styles/ColorPalette.h"
#include "../../../services/config/I18n.h"
#include <algorithm>
#include <cmath>

namespace BeatMate::UI {

AnalysisProgressCard::AnalysisProgressCard()
{
    m_bar = std::make_unique<AnimatedProgressBar>();
    addAndMakeVisible(*m_bar);
}

AnalysisProgressCard::~AnalysisProgressCard()
{
    stopTimer();
}

void AnalysisProgressCard::beginRun()
{
    m_state = RunState::Running;
    m_processed = 0;
    m_total = 0;
    m_skipped = 0;
    m_lastBpm.clear();
    m_lastKey.clear();
    m_lastEnergy.clear();
    m_activeTracks.clear();
    m_startTimeMs = juce::Time::getMillisecondCounterHiRes();
    m_bar->setProgress(0.0f);
    startTimerHz(30);
    repaint();
}

void AnalysisProgressCard::setProgress(int processed, int total, int skipped)
{
    m_processed = processed;
    m_total = total;
    m_skipped = skipped;
    m_bar->setProgress(total > 0 ? static_cast<float>(processed) / static_cast<float>(total) : 0.0f);
    repaint();
}

void AnalysisProgressCard::trackStarted(const juce::String& path, const juce::String& title)
{
    m_activeTracks.push_back({ path, title });
    repaint();
}

void AnalysisProgressCard::trackFinished(const juce::String& path)
{
    m_activeTracks.erase(std::remove_if(m_activeTracks.begin(), m_activeTracks.end(),
                                        [&path](const ActiveTrack& t) { return t.path == path; }),
                         m_activeTracks.end());
    repaint();
}

void AnalysisProgressCard::setLastResult(const juce::String& bpm, const juce::String& key, const juce::String& energy)
{
    m_lastBpm = bpm;
    m_lastKey = key;
    m_lastEnergy = energy;
    repaint();
}

void AnalysisProgressCard::endRun(bool cancelled)
{
    m_state = cancelled ? RunState::Cancelled : RunState::Done;
    m_activeTracks.clear();
    if (!cancelled && m_total > 0)
        m_bar->setProgress(1.0f);
    stopTimer();
    repaint();
}

void AnalysisProgressCard::retranslateUi()
{
    repaint();
}

void AnalysisProgressCard::timerCallback()
{
    m_pulsePhase += 0.10f;
    if (m_pulsePhase > juce::MathConstants<float>::twoPi)
        m_pulsePhase -= juce::MathConstants<float>::twoPi;
    m_spinnerAngle += 0.12f;
    if (m_spinnerAngle > juce::MathConstants<float>::twoPi)
        m_spinnerAngle -= juce::MathConstants<float>::twoPi;
    repaint();
}

void AnalysisProgressCard::resized()
{
    m_bar->setBounds(16, 44, juce::jmax(60, getWidth() - 130), 16);
}

void AnalysisProgressCard::paint(juce::Graphics& g)
{
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());
    if (h < 8.0f)
        return;

    ProDraw::glassPanel(g, { 0.0f, 0.0f, w, h }, 12.0f);

    juce::Colour stateColour;
    juce::String stateText;
    switch (m_state) {
        case RunState::Running:
            stateColour = Colors::primary();
            stateText = BM_TJ("analysis.progress.running");
            break;
        case RunState::Done:
            stateColour = Colors::success();
            stateText = BM_TJ("analysis.progress.done");
            break;
        case RunState::Cancelled:
            stateColour = Colors::warning();
            stateText = BM_TJ("analysis.progress.cancelled");
            break;
        case RunState::Idle:
        default:
            stateColour = Colors::textMuted();
            stateText = BM_TJ("analysis.progress.idle");
            break;
    }

    const float pillW = juce::jmax(120.0f,
        juce::GlyphArrangement::getStringWidth(Type::label(), stateText) + 40.0f);
    ProDraw::statusPill(g, stateText, { 16.0f, 12.0f, pillW, 22.0f }, stateColour,
                        m_state == RunState::Running);

    const int percent = (m_total > 0) ? juce::jlimit(0, 100, m_processed * 100 / m_total) : 0;

    {
        const float ringCx = w - 58.0f;
        const float ringCy = h * 0.5f;
        const float ringR = 26.0f;
        const float thickness = 4.5f;

        g.setColour(Colors::bgElevated());
        juce::Path bgRing;
        bgRing.addCentredArc(ringCx, ringCy, ringR, ringR, 0.0f,
                             0.0f, juce::MathConstants<float>::twoPi, true);
        g.strokePath(bgRing, juce::PathStrokeType(thickness));

        if (m_state == RunState::Done) {
            g.setColour(Colors::success());
            g.strokePath(bgRing, juce::PathStrokeType(thickness));
            g.setFont(juce::Font(20.0f, juce::Font::bold));
            g.drawText(juce::CharPointer_UTF8("\xe2\x9c\x93"),
                       static_cast<int>(ringCx - 14), static_cast<int>(ringCy - 11), 28, 22,
                       juce::Justification::centred);
        } else {
            const float progressAngle = juce::MathConstants<float>::twoPi
                * (m_total > 0 ? static_cast<float>(m_processed) / static_cast<float>(m_total) : 0.0f);
            if (progressAngle > 0.0f) {
                juce::ColourGradient arcGrad(Colors::primary(), ringCx - ringR, ringCy,
                                             Colors::secondary(), ringCx + ringR, ringCy, false);
                g.setGradientFill(arcGrad);
                juce::Path arc;
                arc.addCentredArc(ringCx, ringCy, ringR, ringR, 0.0f,
                                  -juce::MathConstants<float>::halfPi,
                                  -juce::MathConstants<float>::halfPi + progressAngle, true);
                g.strokePath(arc, juce::PathStrokeType(thickness, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
            }
            if (m_state == RunState::Running) {
                const float dotAngle = m_spinnerAngle - juce::MathConstants<float>::halfPi;
                const float dotX = ringCx + ringR * std::cos(dotAngle);
                const float dotY = ringCy + ringR * std::sin(dotAngle);
                const float pulseAlpha = 0.5f + 0.5f * std::sin(m_pulsePhase);
                g.setColour(Colors::primary().withAlpha(pulseAlpha));
                g.fillEllipse(dotX - 4.0f, dotY - 4.0f, 8.0f, 8.0f);
            }
            g.setFont(juce::Font(14.0f, juce::Font::bold));
            g.setColour(Colors::textPrimary());
            g.drawText(juce::String(percent) + "%",
                       static_cast<int>(ringCx - ringR), static_cast<int>(ringCy - 9),
                       static_cast<int>(ringR * 2.0f), 18, juce::Justification::centred);
        }
    }

    int statsY = 70;
    g.setFont(Type::body());
    g.setColour(Colors::textSecondary());
    juce::String statsLine = BM_TJ("analysis.progress.analyzed") + " " + juce::String(m_processed)
        + " / " + juce::String(m_total);
    g.drawText(statsLine, 16, statsY, 220, 18, juce::Justification::centredLeft);

    if (m_skipped > 0) {
        g.setColour(Colors::warning());
        g.drawText(BM_TJ("analysis.progress.skipped") + " " + juce::String(m_skipped),
                   240, statsY, 200, 18, juce::Justification::centredLeft);
    }

    if (m_state == RunState::Running && m_processed > 0 && m_processed < m_total) {
        const double elapsedMs = juce::Time::getMillisecondCounterHiRes() - m_startTimeMs;
        const double msPerTrack = elapsedMs / static_cast<double>(m_processed);
        const int remainingSec = static_cast<int>(msPerTrack * (m_total - m_processed) / 1000.0);
        juce::String timeStr = (remainingSec < 60)
            ? "~" + juce::String(remainingSec) + " s"
            : "~" + juce::String(remainingSec / 60) + " min";
        g.setColour(Colors::textMuted());
        g.drawText(BM_TJ("analysis.progress.eta") + " " + timeStr,
                   450, statsY, 240, 18, juce::Justification::centredLeft);
    }

    int activeY = statsY + 24;
    if (m_state == RunState::Running && !m_activeTracks.empty()) {
        const int maxShown = juce::jmin(3, static_cast<int>(m_activeTracks.size()));
        int ax = 16;
        for (int i = 0; i < maxShown; ++i) {
            const float dotAlpha = 0.4f + 0.6f * (0.5f + 0.5f * std::sin(m_pulsePhase + i * 1.3f));
            g.setColour(Colors::primary().withAlpha(dotAlpha));
            g.fillEllipse(static_cast<float>(ax), static_cast<float>(activeY + 6), 6.0f, 6.0f);
            g.setColour(Colors::textPrimary());
            g.setFont(juce::Font(12.0f));
            const int tw = juce::jmin(260, (getWidth() - 130) / juce::jmax(1, maxShown) - 20);
            g.drawText(m_activeTracks[static_cast<size_t>(i)].title,
                       ax + 12, activeY, tw, 18, juce::Justification::centredLeft, true);
            ax += tw + 24;
        }
        if (static_cast<int>(m_activeTracks.size()) > maxShown) {
            g.setColour(Colors::textMuted());
            g.drawText("+" + juce::String(static_cast<int>(m_activeTracks.size()) - maxShown),
                       ax, activeY, 40, 18, juce::Justification::centredLeft);
        }
    }

    int badgesY = activeY + 26;
    if (m_lastBpm.isNotEmpty() || m_lastKey.isNotEmpty()) {
        float bx = 16.0f;
        if (m_lastBpm.isNotEmpty()) {
            ProDraw::badge(g, "BPM " + m_lastBpm, bx, static_cast<float>(badgesY), 92.0f, 20.0f, Colors::bpmBadge());
            bx += 100.0f;
        }
        if (m_lastKey.isNotEmpty()) {
            ProDraw::badge(g, "KEY " + m_lastKey, bx, static_cast<float>(badgesY), 92.0f, 20.0f, Colors::keyBadge());
            bx += 100.0f;
        }
        if (m_lastEnergy.isNotEmpty()) {
            ProDraw::badge(g, "NRG " + m_lastEnergy, bx, static_cast<float>(badgesY), 92.0f, 20.0f, Colors::energyBadge());
            bx += 100.0f;
        }
        g.setFont(juce::Font(10.0f));
        g.setColour(Colors::textMuted());
        g.drawText(BM_TJ("analysis.progress.lastResult"),
                   static_cast<int>(bx + 8), badgesY, 260, 20, juce::Justification::centredLeft);
    }
}

} // namespace BeatMate::UI
