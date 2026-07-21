#include "SetPreparationView.h"
#include "../styles/ColorPalette.h"
#include "../widgets/ToastNotifier.h"
#include "../widgets/browser/LibraryBrowserPanel.h"
#include "../../services/library/TrackDataProvider.h"
#include "../../services/config/I18n.h"
#include "../../services/persistence/SettingsStore.h"
#include "../../services/djsoftware/SendToDJRouter.h"
#include "../../app/ServiceLocator.h"
#include "../utils/ViewPrefs.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <algorithm>
#include <map>
#include <random>
#include <unordered_set>
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>

namespace BeatMate {
extern ServiceLocator* g_serviceLocator;
} // namespace BeatMate

namespace BeatMate::UI {

int SetPreparationView::SetTimelineComponent::trackAtX(float x) const
{
    if (!tracks || tracks->empty()) return -1;
    double totalSec = 0.0;
    for (auto& t : *tracks) totalSec += (t.duration > 0 ? t.duration : 180.0);
    if (totalSec <= 0) return -1;

    float px = 8.0f;
    float totalW = (float)getWidth() - 16.0f;
    for (int i = 0; i < (int)tracks->size(); ++i)
    {
        double d = (*tracks)[i].duration > 0 ? (*tracks)[i].duration : 180.0;
        float w = (float)(d / totalSec) * totalW;
        if (x >= px && x < px + w) return i;
        px += w;
    }
    return -1;
}

void SetPreparationView::SetTimelineComponent::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    {
        juce::ColourGradient bgGrad(Colors::bgMedium().withAlpha(0.6f), b.getX(), b.getY(),
                                     Colors::bgDark().withAlpha(0.4f), b.getX(), b.getBottom(), false);
        g.setGradientFill(bgGrad);
        g.fillRoundedRectangle(b, 10.0f);
    }
    g.setColour(Colors::border().withAlpha(0.4f));
    g.drawRoundedRectangle(b, 10.0f, 1.5f);

    g.setColour(Colors::warning().withAlpha(0.6f));
    g.fillRoundedRectangle(8.0f, 6.0f, 3.0f, 12.0f, 1.5f);
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.setColour(Colors::textSecondary());
    g.drawText(juce::String::fromUTF8("DEROULE DU SET"), 16.0f, 4.0f, 160.0f, 16.0f, juce::Justification::centredLeft);

    if (!tracks || tracks->empty())
    {
        g.setFont(juce::Font(12.0f));
        g.setColour(Colors::textMuted());
        g.drawText(juce::String::fromUTF8("Ajoutez des morceaux pour visualiser le set"),
                   b, juce::Justification::centred);
        return;
    }

    double totalSec = 0.0;
    for (auto& t : *tracks) totalSec += (t.duration > 0 ? t.duration : 180.0);
    if (totalSec <= 0) totalSec = 1;

    float barY = 24.0f;
    float barH = b.getHeight() - 48.0f;
    float totalW = b.getWidth() - 16.0f;

    g.setFont(juce::Font(9.0f));
    g.setColour(Colors::textDim());
    double cumSec = 0;
    float cumPx = 8.0f;
    for (int i = 0; i <= (int)tracks->size(); ++i)
    {
        if (i == 0 || i == (int)tracks->size() || (i % juce::jmax(1, (int)tracks->size() / 6) == 0))
        {
            int mins = (int)(cumSec / 60.0);
            juce::String timeStr = juce::String(mins / 60) + "h" + (mins % 60 < 10 ? "0" : "") + juce::String(mins % 60);
            g.drawText(timeStr, (int)(cumPx - 14), (int)(barY + barH + 4), 34, 14, juce::Justification::centred);
        }
        if (i < (int)tracks->size())
        {
            double d = (*tracks)[i].duration > 0 ? (*tracks)[i].duration : 180.0;
            cumSec += d;
            cumPx += (float)(d / totalSec) * totalW;
        }
    }

    float px = 8.0f;
    for (int i = 0; i < (int)tracks->size(); ++i)
    {
        auto& t = (*tracks)[i];
        double d = t.duration > 0 ? t.duration : 180.0;
        float w = (float)(d / totalSec) * totalW;
        bool isHover = (i == hovered);

        float eNorm = juce::jlimit(0.0f, 1.0f, t.energy / 10.0f);
        juce::Colour eCol = eNorm < 0.35f ? Colors::success()
                           : eNorm < 0.65f ? Colors::warning()
                           : Colors::error();
        float blockH = juce::jmax(10.0f, barH * (0.35f + 0.65f * eNorm));
        float blockY = barY + (barH - blockH);

        {
            juce::ColourGradient grad(eCol.withAlpha(isHover ? 0.65f : 0.45f), px, blockY,
                                      eCol.withAlpha(isHover ? 0.30f : 0.15f), px, blockY + blockH, false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(px, blockY, juce::jmax(1.0f, w - 2.0f), blockH, 3.0f);
        }
        g.setColour(eCol.withAlpha(0.7f));
        g.drawRoundedRectangle(px, blockY, juce::jmax(1.0f, w - 2.0f), blockH, 3.0f, isHover ? 1.6f : 0.8f);

        if (w > 34.0f)
        {
            g.setFont(juce::Font(8.5f, juce::Font::bold));
            g.setColour(Colors::textPrimary().withAlpha(0.85f));
            g.drawText(juce::String(i + 1), (int)(px + 3), (int)(barY + 2), 18, 12, juce::Justification::centredLeft);
        }
        px += w;
    }
}

void SetPreparationView::SetTimelineComponent::mouseMove(const juce::MouseEvent& e)
{
    int h = trackAtX((float)e.x);
    if (h != hovered) { hovered = h; repaint(); }
}

void SetPreparationView::SetTimelineComponent::mouseDown(const juce::MouseEvent& e)
{
    int idx = trackAtX((float)e.x);
    if (idx >= 0 && onTrackSelected) onTrackSelected(idx);
}

void SetPreparationView::ChecklistComponent::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    {
        juce::ColourGradient grad(Colors::bgCard().withAlpha(0.7f), b.getX(), b.getY(),
                                  Colors::bgCard().withAlpha(0.5f), b.getX(), b.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(b, 10.0f);
    }
    g.setColour(Colors::border().withAlpha(0.3f));
    g.drawRoundedRectangle(b, 10.0f, 1.5f);

    g.setColour(Colors::primary().withAlpha(0.7f));
    g.fillRoundedRectangle(8.0f, 4.0f, 3.0f, 16.0f, 1.5f);
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.setColour(Colors::textPrimary());
    g.drawText(juce::String::fromUTF8("CHECKLIST PRE-SET"), 16.0f, 4.0f, 220.0f, 16.0f, juce::Justification::centredLeft);

    float y = 28.0f;
    for (size_t idx = 0; idx < items.size(); ++idx)
    {
        auto& item = items[idx];
        bool checked = item.value ? *item.value : false;

        juce::Rectangle<float> rowBg(6.0f, y, b.getWidth() - 12.0f, 28.0f);
        if (checked)
        {
            g.setColour(Colors::success().withAlpha(0.06f));
            g.fillRoundedRectangle(rowBg, 6.0f);
        }
        else if (idx % 2 == 0)
        {
            g.setColour(Colors::bgLightest().withAlpha(0.10f));
            g.fillRoundedRectangle(rowBg, 6.0f);
        }

        juce::Rectangle<float> box(14.0f, y + 5.0f, 18.0f, 18.0f);
        if (checked)
        {
            g.setColour(Colors::success().withAlpha(0.12f));
            g.fillRoundedRectangle(box.expanded(4.0f), 7.0f);
            g.setColour(Colors::success().withAlpha(0.08f));
            g.fillRoundedRectangle(box.expanded(7.0f), 9.0f);
            g.setColour(Colors::success().withAlpha(0.35f));
            g.fillRoundedRectangle(box, 5.0f);
            g.setColour(Colors::success());
            g.drawRoundedRectangle(box, 5.0f, 1.5f);
            juce::Path tick;
            tick.startNewSubPath(box.getX() + 4, box.getCentreY());
            tick.lineTo(box.getCentreX() - 1, box.getBottom() - 4);
            tick.lineTo(box.getRight() - 4, box.getY() + 4);
            g.setColour(Colors::success());
            g.strokePath(tick, juce::PathStrokeType(2.5f));
        }
        else
        {
            g.setColour(Colors::bgLightest().withAlpha(0.4f));
            g.fillRoundedRectangle(box, 5.0f);
            g.setColour(Colors::border().withAlpha(0.5f));
            g.drawRoundedRectangle(box, 5.0f, 1.0f);
        }

        g.setFont(juce::Font(11.0f));
        g.setColour(checked ? Colors::textPrimary() : Colors::textSecondary());
        g.drawText(item.label, 40.0f, y, b.getWidth() - 50.0f, 28.0f, juce::Justification::centredLeft);
        y += 30.0f;
    }
}

void SetPreparationView::ChecklistComponent::mouseDown(const juce::MouseEvent& e)
{
    float y = 28.0f;
    const float rowH = 30.0f;
    for (auto& item : items)
    {
        if ((float)e.y >= y && (float)e.y < y + rowH && item.value)
        {
            *item.value = !(*item.value);
            repaint();
            if (onChecklistChanged) onChecklistChanged();
            return;
        }
        y += rowH;
    }
}

void SetPreparationView::SetScoreCircle::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    {
        juce::ColourGradient bgGrad(Colors::bgMedium().withAlpha(0.7f), b.getCentreX(), b.getY(),
                                     Colors::bgCard().withAlpha(0.5f), b.getCentreX(), b.getBottom(), false);
        g.setGradientFill(bgGrad);
        g.fillRoundedRectangle(b, 12.0f);
    }
    g.setColour(Colors::border().withAlpha(0.3f));
    g.drawRoundedRectangle(b, 12.0f, 1.0f);

    float cx = b.getCentreX();
    float cy = b.getCentreY() - 6.0f;
    float radius = juce::jmin(b.getWidth(), b.getHeight()) * 0.35f;
    float arcThickness = 7.0f;

    float startAngle = -juce::MathConstants<float>::pi * 0.8f;
    float endAngle   =  juce::MathConstants<float>::pi * 0.8f;
    float totalArc   = endAngle - startAngle;

    juce::Path arcBg;
    arcBg.addCentredArc(cx, cy, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour(Colors::bgLightest().withAlpha(0.5f));
    g.strokePath(arcBg, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    float scoreNorm = score / 100.0f;
    float valueAngle = startAngle + scoreNorm * totalArc;

    if (score > 0)
    {
        int numSegments = juce::jmax(2, (int)(scoreNorm * 30));
        for (int s = 0; s < numSegments; ++s)
        {
            float t0 = s / (float)numSegments;
            float t1 = (s + 1) / (float)numSegments;
            float a0 = startAngle + t0 * scoreNorm * totalArc;
            float a1 = startAngle + t1 * scoreNorm * totalArc;

            float tMid = (t0 + t1) * 0.5f;
            juce::Colour segCol;
            if (score >= 80)
                segCol = Colors::success().interpolatedWith(juce::Colour(0xFF34D399), tMid);
            else if (score >= 50)
                segCol = Colors::warning().interpolatedWith(Colors::energyMedium(), tMid);
            else
                segCol = Colors::error().interpolatedWith(Colors::warning(), tMid);

            juce::Path seg;
            seg.addCentredArc(cx, cy, radius, radius, 0.0f, a0, a1, true);
            g.setColour(segCol);
            g.strokePath(seg, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        float tipX = cx + radius * std::cos(valueAngle);
        float tipY = cy + radius * std::sin(valueAngle);
        juce::Colour tipCol = score >= 80 ? Colors::success() : score >= 50 ? Colors::warning() : Colors::error();
        g.setColour(tipCol.withAlpha(0.3f));
        g.fillEllipse(tipX - 10, tipY - 10, 20, 20);
        g.setColour(tipCol.withAlpha(0.6f));
        g.fillEllipse(tipX - 5, tipY - 5, 10, 10);
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.fillEllipse(tipX - 3, tipY - 3, 6, 6);
    }

    juce::Colour scoreCol = score >= 80 ? Colors::success() : score >= 50 ? Colors::warning() : Colors::error();
    g.setColour(scoreCol.withAlpha(0.15f));
    g.setFont(juce::Font(26.0f, juce::Font::bold));
    g.drawText(juce::String(score), (int)(cx - 26), (int)(cy - 15), 52, 30, juce::Justification::centred);
    g.setFont(juce::Font(22.0f, juce::Font::bold));
    g.setColour(Colors::textPrimary());
    g.drawText(juce::String(score), (int)(cx - 24), (int)(cy - 14), 48, 28, juce::Justification::centred);

    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.setColour(Colors::textMuted());
    g.drawText("SET SCORE", b.getX(), cy + radius + 10, b.getWidth(), 14, juce::Justification::centred);
}

juce::Colour SetPreparationView::EnergyCurveGraph::getEnergyColor(float normalized) const
{
    if (normalized <= 0.0f) return juce::Colour(0xFF22C55E);
    if (normalized >= 1.0f) return juce::Colour(0xFFEF4444);
    if (normalized < 0.5f)
        return juce::Colour(0xFF22C55E).interpolatedWith(juce::Colour(0xFFF59E0B), normalized * 2.0f);
    return juce::Colour(0xFFF59E0B).interpolatedWith(juce::Colour(0xFFEF4444), (normalized - 0.5f) * 2.0f);
}

void SetPreparationView::EnergyCurveGraph::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    {
        juce::ColourGradient bgGrad(Colors::bgMedium().withAlpha(0.6f), b.getX(), b.getY(),
                                     Colors::bgCard().withAlpha(0.4f), b.getX(), b.getBottom(), false);
        g.setGradientFill(bgGrad);
        g.fillRoundedRectangle(b, 10.0f);
    }
    g.setColour(Colors::border().withAlpha(0.3f));
    g.drawRoundedRectangle(b, 10.0f, 1.0f);

    g.setFont(juce::Font("Segoe UI", 11.0f, juce::Font::bold));
    g.setColour(Colors::textPrimary());
    g.drawText("ENERGY CURVE", 12.0f, 4.0f, 130.0f, 16.0f, juce::Justification::centredLeft);
    g.setColour(Colors::accent());
    g.fillRoundedRectangle(12.0f, 20.0f, 80.0f, 2.5f, 1.25f);

    if (energyValues.empty()) return;

    bool allZero = true;
    for (auto e : energyValues) if (e > 0) { allZero = false; break; }
    if (allZero)
    {
        g.setFont(juce::Font(13.0f));
        g.setColour(Colors::textMuted().withAlpha(0.7f));
        g.drawText(juce::CharPointer_UTF8("Analysez vos pistes pour voir la courbe d'\xc3\xa9nergie"),
                   b.reduced(20.0f, 30.0f), juce::Justification::centred);
        return;
    }

    float plotX = 12.0f, plotY = 24.0f;
    float plotW = b.getWidth() - 24.0f, plotH = b.getHeight() - 40.0f;
    if (plotH < 20.0f) return;

    g.saveState();
    g.reduceClipRegion((int)(plotX - 2), (int)(plotY - 2), (int)(plotW + 4), (int)(plotH + 4));

    g.setFont(juce::Font(8.0f));
    for (int i = 0; i <= 10; i += 2)
    {
        float y = plotY + plotH - (i / 10.0f) * plotH;
        g.setColour(Colors::bgLightest().withAlpha(0.3f));
        g.drawHorizontalLine((int)y, plotX, plotX + plotW);
    }

    int n = (int)energyValues.size();
    if (n < 1) { g.restoreState(); return; }

    const float spacing = (n > 1) ? 1.0f / (float)(n - 1) : 1.0f;
    auto trackX = [&](int idx) -> float {
        return (n == 1) ? plotX + plotW * 0.5f : plotX + (idx / (float)(n - 1)) * plotW;
    };

    struct CurvePt { float x; float e; };
    std::vector<CurvePt> pts;
    for (int i = 0; i < n; ++i)
    {
        const std::vector<int>* intra = (i < (int)intraCurves.size() && intraCurves[i].size() > 1)
                                        ? &intraCurves[i] : nullptr;
        if (intra != nullptr && n > 1)
        {
            const int k = (int)intra->size();
            for (int j = 0; j < k; ++j)
            {
                float off = ((j + 0.5f) / (float)k - 0.5f) * spacing * 0.85f * plotW;
                pts.push_back({ trackX(i) + off, (float)(*intra)[j] });
            }
        }
        else
        {
            pts.push_back({ trackX(i), (float)energyValues[i] });
        }
    }

    auto ptXY = [&](const CurvePt& p) -> juce::Point<float> {
        return { p.x, plotY + plotH - (p.e / 10.0f) * plotH };
    };
    auto energyAtX = [&](float x) -> float {
        if (x <= pts.front().x) return pts.front().e;
        if (x >= pts.back().x) return pts.back().e;
        for (size_t i = 1; i < pts.size(); ++i)
        {
            if (x <= pts[i].x)
            {
                float span = pts[i].x - pts[i - 1].x;
                float frac = span > 0.0f ? (x - pts[i - 1].x) / span : 0.0f;
                return pts[i - 1].e * (1.0f - frac) + pts[i].e * frac;
            }
        }
        return pts.back().e;
    };

    juce::Path curvePath;
    {
        curvePath.startNewSubPath(ptXY(pts.front()));
        for (size_t i = 1; i < pts.size(); ++i)
        {
            auto prev = ptXY(pts[i - 1]);
            auto curr = ptXY(pts[i]);
            float cpX = (prev.x + curr.x) * 0.5f;
            curvePath.cubicTo(cpX, prev.y, cpX, curr.y, curr.x, curr.y);
        }
    }

    {
        juce::Path fillPath(curvePath);
        fillPath.lineTo(pts.back().x, plotY + plotH);
        fillPath.lineTo(pts.front().x, plotY + plotH);
        fillPath.closeSubPath();

        int numSlices = juce::jmin((int)pts.size() * 4, 80);
        for (int s = 0; s < numSlices; ++s)
        {
            float sliceX0 = plotX + s / (float)numSlices * plotW;
            float sliceX1 = plotX + (s + 1) / (float)numSlices * plotW;
            juce::Colour sliceCol = getEnergyColor(energyAtX((sliceX0 + sliceX1) * 0.5f) / 10.0f);

            juce::ColourGradient sliceGrad(sliceCol.withAlpha(0.35f), sliceX0, plotY,
                                            sliceCol.withAlpha(0.03f), sliceX0, plotY + plotH, false);
            g.setGradientFill(sliceGrad);
            g.saveState();
            g.reduceClipRegion(fillPath);
            g.fillRect(sliceX0, plotY, sliceX1 - sliceX0 + 1.0f, plotH);
            g.restoreState();
        }
    }

    for (size_t i = 1; i < pts.size(); ++i)
    {
        auto prev = ptXY(pts[i - 1]);
        auto curr = ptXY(pts[i]);
        juce::Colour segCol = getEnergyColor((pts[i - 1].e + pts[i].e) / 20.0f);

        juce::Path seg;
        seg.startNewSubPath(prev);
        float cpX = (prev.x + curr.x) * 0.5f;
        seg.cubicTo(cpX, prev.y, cpX, curr.y, curr.x, curr.y);

        g.setColour(segCol.withAlpha(0.15f));
        g.strokePath(seg, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved));
        g.setColour(segCol);
        g.strokePath(seg, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved));
    }

    for (int i = 0; i < n; ++i)
    {
        float x = trackX(i);
        float e = energyAtX(x);
        float y = plotY + plotH - (e / 10.0f) * plotH;
        juce::Colour dotCol = getEnergyColor(e / 10.0f);

        g.setColour(dotCol.withAlpha(0.2f));
        g.fillEllipse(x - 9, y - 9, 18, 18);
        g.setColour(dotCol.withAlpha(0.4f));
        g.fillEllipse(x - 6, y - 6, 12, 12);
        g.setColour(dotCol);
        g.fillEllipse(x - 4, y - 4, 8, 8);
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.fillEllipse(x - 2, y - 2, 4, 4);
    }

    g.restoreState();
}

void SetPreparationView::StatisticsDashboard::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    ProDraw::glassPanel(g, b, 10.0f);

    g.setFont(juce::Font("Segoe UI", 10.0f, juce::Font::bold));
    g.setColour(Colors::textSecondary());
    g.drawText("STATISTIQUES SET", 10, 4, (int)b.getWidth() - 20, 14, juce::Justification::centredLeft);
    g.setColour(Colors::accent());
    g.fillRoundedRectangle(10.0f, 18.0f, 60.0f, 2.0f, 1.0f);

    if (stats.trackCount == 0) return;

    int y = 24;

    ProDraw::badge(g, "BPM", 10, (float)y, 36, 16, Colors::bpmBadge());
    g.setFont(juce::Font(10.0f));
    g.setColour(Colors::textPrimary());
    g.drawText("Min:" + juce::String(stats.bpm.min, 0) + " Max:" + juce::String(stats.bpm.max, 0) +
               " Moy:" + juce::String(stats.bpm.mean, 1),
               52, y, (int)b.getWidth() - 62, 16, juce::Justification::centredLeft);
    y += 20;

    if (!stats.bpm.histogram.empty())
    {
        int maxCount = 0;
        for (auto& [bucket, count] : stats.bpm.histogram)
            maxCount = juce::jmax(maxCount, count);

        float barX = 10.0f;
        float barW = (b.getWidth() - 20.0f) / (float)stats.bpm.histogram.size();
        for (auto& [bucket, count] : stats.bpm.histogram)
        {
            float norm = maxCount > 0 ? count / (float)maxCount : 0.0f;
            float barH = norm * 30.0f;
            g.setColour(Colors::bpmBadge().withAlpha(0.5f));
            g.fillRoundedRectangle(barX + 1, y + 30.0f - barH, barW - 2, barH, 2.0f);
            barX += barW;
        }
        y += 34;
    }

    ProDraw::badge(g, "NRG", 10, (float)y, 36, 16, Colors::energyBadge());
    g.setFont(juce::Font(10.0f));
    g.setColour(Colors::textPrimary());
    g.drawText("Min:" + juce::String((int)stats.energy.min) + " Max:" + juce::String((int)stats.energy.max) +
               " Moy:" + juce::String(stats.energy.mean, 1),
               52, y, (int)b.getWidth() - 62, 16, juce::Justification::centredLeft);
    y += 20;

    if (stats.genres.uniqueGenres > 0)
    {
        ProDraw::badge(g, "GENRES", 10, (float)y, 48, 16, Colors::secondary());
        y += 18;
        g.setFont(juce::Font(9.0f));
        for (auto& [genre, pct] : stats.genres.percentages)
        {
            if (y > b.getBottom() - 16) break;
            float barW = (b.getWidth() - 80.0f) * (pct / 100.0f);
            g.setColour(Colors::secondary().withAlpha(0.3f));
            g.fillRoundedRectangle(58.0f, (float)y + 1, barW, 10.0f, 3.0f);
            g.setColour(Colors::textSecondary());
            g.drawText(juce::String(genre), 10, y, 46, 12, juce::Justification::centredLeft, true);
            g.drawText(juce::String((int)pct) + "%", 58.0f + barW + 4, (float)y, 30, 12, juce::Justification::centredLeft);
            y += 14;
        }
    }

    y += 4;
    if (stats.transitions.totalTransitions > 0)
    {
        float compRate = stats.transitions.compatibilityRate * 100.0f;
        juce::Colour rateCol = compRate >= 75 ? Colors::success() : compRate >= 50 ? Colors::warning() : Colors::error();
        ProDraw::badge(g, juce::String((int)compRate) + "%", 10, (float)y, 40, 16, rateCol);
        g.setFont(juce::Font(9.0f));
        g.setColour(Colors::textSecondary());
        g.drawText("transitions compatibles", 54, y, (int)b.getWidth() - 64, 16, juce::Justification::centredLeft);
    }
}

SetPreparationView::TransitionDetailEditor::TransitionDetailEditor()
{
    scoreLabel = std::make_unique<juce::Label>("score", "");
    scoreLabel->setFont(juce::Font(14.0f, juce::Font::bold));
    addAndMakeVisible(scoreLabel.get());

    verdictLabel = std::make_unique<juce::Label>("verdict", "");
    verdictLabel->setFont(juce::Font(11.0f, juce::Font::bold));
    addAndMakeVisible(verdictLabel.get());

    bpmLabel = std::make_unique<juce::Label>("bpm", "");
    bpmLabel->setFont(juce::Font(10.0f));
    addAndMakeVisible(bpmLabel.get());

    keyLabel = std::make_unique<juce::Label>("key", "");
    keyLabel->setFont(juce::Font(10.0f));
    addAndMakeVisible(keyLabel.get());

    energyLabel = std::make_unique<juce::Label>("energy", "");
    energyLabel->setFont(juce::Font(10.0f));
    addAndMakeVisible(energyLabel.get());

    adviceLabel = std::make_unique<juce::Label>("advice", "");
    adviceLabel->setFont(juce::Font(10.0f));
    adviceLabel->setColour(juce::Label::textColourId, Colors::accent());
    addAndMakeVisible(adviceLabel.get());

    mixPointLabel = std::make_unique<juce::Label>("mix", "");
    mixPointLabel->setFont(juce::Font(10.0f));
    addAndMakeVisible(mixPointLabel.get());

    transitionTypeCombo = std::make_unique<juce::ComboBox>("transType");
    transitionTypeCombo->addItem(BM_TJ("setPrep.settings.transType.harmonic"), 1);
    transitionTypeCombo->addItem(BM_TJ("setPrep.settings.transType.eqBlend"), 2);
    transitionTypeCombo->addItem(BM_TJ("setPrep.settings.transType.filterSweep"), 3);
    transitionTypeCombo->addItem(BM_TJ("setPrep.settings.transType.echoOut"), 4);
    transitionTypeCombo->addItem(BM_TJ("setPrep.settings.transType.cut"), 5);
    transitionTypeCombo->addItem(BM_TJ("setPrep.settings.transType.backspin"), 6);
    transitionTypeCombo->setTooltip(BM_TJ("setPrep.settings.transTypeTip"));
    transitionTypeCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    transitionTypeCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    transitionTypeCombo->onChange = [this] {
        if (adviceLabel && transitionTypeCombo)
            adviceLabel->setText(BM_TJ("setPrep.label.transition") + " " + transitionTypeCombo->getText(),
                                 juce::dontSendNotification);
        Prefs::setInt("setPrep.transitionTypeId", transitionTypeCombo->getSelectedId());
    };
    addAndMakeVisible(transitionTypeCombo.get());

    mixDurationSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    mixDurationSlider->setRange(4, 64, 4);
    mixDurationSlider->setValue(16);
    mixDurationSlider->setTextValueSuffix(" beats");
    mixDurationSlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 74, 18);
    mixDurationSlider->setTooltip(BM_TJ("setPrep.settings.mixDurationTip"));
    mixDurationSlider->setColour(juce::Slider::trackColourId, Colors::primary());
    mixDurationSlider->onValueChange = [this] {
        if (mixPointLabel && mixDurationSlider)
        {
            auto current = mixPointLabel->getText();
            auto base = current.upToFirstOccurrenceOf("|", false, false).trim();
            if (base.isEmpty()) base = BM_TJ("setPrep.label.mixPoint") + " -";
            mixPointLabel->setText(base + " | " + juce::String((int)mixDurationSlider->getValue()) + " beats",
                                   juce::dontSendNotification);
        }
        Prefs::setInt("setPrep.mixDurationBeats", (int)mixDurationSlider->getValue());
    };
    addAndMakeVisible(mixDurationSlider.get());
}

void SetPreparationView::TransitionDetailEditor::updateFromResult(
    const Services::Preparation::MatchUpResult& result, int idx, int rowScorePercent)
{
    matchResult = result;
    transitionIndex = idx;
    visible = true;

    float pct = rowScorePercent >= 0 ? (float)rowScorePercent : result.overallScore * 100.0f;
    juce::Colour col = pct >= 75 ? Colors::success() : pct >= 50 ? Colors::warning() : Colors::error();
    scoreLabel->setText(juce::String((int)pct) + "%", juce::dontSendNotification);
    scoreLabel->setColour(juce::Label::textColourId, col);

    verdictLabel->setText(juce::String(result.verdict), juce::dontSendNotification);
    verdictLabel->setColour(juce::Label::textColourId, col);

    juce::String detailText;
    for (auto& d : result.details)
        detailText += juce::String(d.criterion) + ": " + juce::String(d.explanation) + "\n";

    auto keyOf = [](const Models::Track& t) {
        juce::String k = juce::String(t.camelotKey).trim();
        if (k.isEmpty()) k = juce::String(t.key).trim();
        return k.isEmpty() ? juce::String("-") : k;
    };
    auto energyOf = [](float e) {
        return e > 0.0f ? juce::String((int)e) : juce::String("-");
    };
    bpmLabel->setText("BPM: " + juce::String(result.trackA.bpm, 1) + " -> " +
                      juce::String(result.trackB.bpm, 1), juce::dontSendNotification);
    keyLabel->setText("Cle: " + keyOf(result.trackA) + " -> " +
                      keyOf(result.trackB), juce::dontSendNotification);
    energyLabel->setText(BM_TJ("setPrep.label.energy") + " " + energyOf(result.trackA.energy) + " -> " +
                         energyOf(result.trackB.energy), juce::dontSendNotification);
    adviceLabel->setText(BM_TJ("setPrep.label.transition") + " " + juce::String(result.suggestedTransition), juce::dontSendNotification);
    mixPointLabel->setText(BM_TJ("setPrep.label.mixPoint") + " " + juce::String(result.suggestedMixPoint, 1) + "s | " +
                           juce::String((int)result.mixDurationBeats) + " beats", juce::dontSendNotification);
    mixDurationSlider->setValue(result.mixDurationBeats, juce::dontSendNotification);

    if (transitionTypeCombo)
    {
        const juce::String suggested = juce::String(result.suggestedTransition).trim();
        int matchedId = 0;
        for (int i = 0; i < transitionTypeCombo->getNumItems(); ++i)
        {
            if (transitionTypeCombo->getItemText(i).equalsIgnoreCase(suggested))
            {
                matchedId = transitionTypeCombo->getItemId(i);
                break;
            }
        }
        if (matchedId > 0)
            transitionTypeCombo->setSelectedId(matchedId, juce::dontSendNotification);
        else if (transitionTypeCombo->getSelectedId() == 0)
            transitionTypeCombo->setSelectedId(1, juce::dontSendNotification);
    }

    repaint();
}

void SetPreparationView::TransitionDetailEditor::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    ProDraw::glassPanel(g, b, 8.0f);
    g.setFont(juce::Font("Segoe UI", 10.0f, juce::Font::bold));
    g.setColour(Colors::textSecondary());
    g.drawText(BM_TJ("setPrep.transition.title") + " #" + juce::String(transitionIndex + 1),
               10, 2, 220, 14, juce::Justification::centredLeft);
    g.setFont(juce::Font("Segoe UI", 9.0f, juce::Font::italic));
    g.setColour(Colors::textMuted());
    g.drawText(BM_TJ("setPrep.transition.note"),
               10, 2, getWidth() - 20, 14, juce::Justification::centredRight);
}

void SetPreparationView::TransitionDetailEditor::resized()
{
    auto area = getLocalBounds().reduced(10);
    area.removeFromTop(16);

    auto row1 = area.removeFromTop(20);
    scoreLabel->setBounds(row1.removeFromLeft(50));
    verdictLabel->setBounds(row1.removeFromLeft(100));
    transitionTypeCombo->setBounds(row1.removeFromRight(140));

    area.removeFromTop(2);
    auto row2 = area.removeFromTop(16);
    bpmLabel->setBounds(row2.removeFromLeft(row2.getWidth() / 3));
    keyLabel->setBounds(row2.removeFromLeft(row2.getWidth() / 2));
    energyLabel->setBounds(row2);

    area.removeFromTop(2);
    adviceLabel->setBounds(area.removeFromTop(16));
    area.removeFromTop(2);
    auto row4 = area.removeFromTop(20);
    mixPointLabel->setBounds(row4.removeFromLeft(row4.getWidth() / 2));
    mixDurationSlider->setBounds(row4);
}

SetPreparationView::AlgorithmSelector::AlgorithmSelector()
{
    algorithmCombo = std::make_unique<juce::ComboBox>("algo");
    algorithmCombo->addItem("IA Nearest-Neighbor", 1);
    algorithmCombo->addItem("Optimal (exhaustif)", 2);
    algorithmCombo->addItem("Simulated Annealing", 3);
    algorithmCombo->addItem("Pure 2-Opt", 4);
    algorithmCombo->addSeparator();
    algorithmCombo->addItem("Quick: Auto BPM", 10);
    algorithmCombo->addItem("Quick: Auto Energy", 11);
    algorithmCombo->addItem("Quick: Auto Harmonic", 12);
    algorithmCombo->addItem("Quick: Party Mix", 13);
    algorithmCombo->addItem("Quick: Chill Session", 14);
    algorithmCombo->addItem("Quick: Progressive", 15);
    algorithmCombo->setSelectedId(Prefs::getInt("setPrep.algorithmId", 1), juce::dontSendNotification);
    algorithmCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    algorithmCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    addAndMakeVisible(algorithmCombo.get());

    auto makeWeightSlider = [this](const juce::String& name) {
        auto s = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
        s->setRange(0.0, 1.0, 0.05);
        s->setColour(juce::Slider::trackColourId, Colors::primary());
        addAndMakeVisible(s.get());
        return s;
    };
    bpmWeight = makeWeightSlider("bpm"); bpmWeight->setValue(Prefs::getDouble("setPrep.weight.bpm",    0.3));
    keyWeight = makeWeightSlider("key"); keyWeight->setValue(Prefs::getDouble("setPrep.weight.key",    0.4));
    energyWeight = makeWeightSlider("nrg"); energyWeight->setValue(Prefs::getDouble("setPrep.weight.energy", 0.2));
    genreWeight = makeWeightSlider("genre"); genreWeight->setValue(Prefs::getDouble("setPrep.weight.genre",  0.1));
    bpmWeight->onValueChange    = [this] { Prefs::setDouble("setPrep.weight.bpm",    bpmWeight->getValue()); };
    keyWeight->onValueChange    = [this] { Prefs::setDouble("setPrep.weight.key",    keyWeight->getValue()); };
    energyWeight->onValueChange = [this] { Prefs::setDouble("setPrep.weight.energy", energyWeight->getValue()); };
    genreWeight->onValueChange  = [this] { Prefs::setDouble("setPrep.weight.genre",  genreWeight->getValue()); };

    auto makeLbl = [this](const juce::String& text) {
        auto l = std::make_unique<juce::Label>("", text);
        l->setFont(juce::Font(10.0f, juce::Font::bold));
        l->setColour(juce::Label::textColourId, Colors::textSecondary());
        addAndMakeVisible(l.get());
        return l;
    };
    bpmWLabel = makeLbl("BPM");
    keyWLabel = makeLbl("Key");
    energyWLabel = makeLbl("Energy");
    genreWLabel = makeLbl("Genre");

    descriptionLabel = std::make_unique<juce::Label>("desc", juce::String());
    descriptionLabel->setFont(juce::Font(juce::FontOptions{}.withHeight(10.5f)));
    descriptionLabel->setColour(juce::Label::textColourId, Colors::textSecondary());
    descriptionLabel->setJustificationType(juce::Justification::centredLeft);
    descriptionLabel->setMinimumHorizontalScale(0.8f);
    addAndMakeVisible(descriptionLabel.get());

    algorithmCombo->onChange = [this]() {
        updateDescriptionForSelection();
        Prefs::setInt("setPrep.algorithmId", algorithmCombo->getSelectedId());
    };

    applyBtn = std::make_unique<juce::TextButton>(BM_TJ("common.apply"));
    applyBtn->setColour(juce::TextButton::buttonColourId, Colors::primary());
    applyBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    applyBtn->onClick = [this]() {
        int algo = algorithmCombo->getSelectedId();
        int quickMode = -1;
        if (algo >= 10) { quickMode = algo - 10; algo = 0; }
        if (onApply) onApply(algo, (float)bpmWeight->getValue(), (float)keyWeight->getValue(),
                             (float)energyWeight->getValue(), (float)genreWeight->getValue(), quickMode);
    };
    addAndMakeVisible(applyBtn.get());

    cancelBtn = std::make_unique<juce::TextButton>(BM_TJ("common.cancel"));
    cancelBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    cancelBtn->onClick = [this]() { setVisible(false); };
    addAndMakeVisible(cancelBtn.get());

    closeBtn = std::make_unique<juce::TextButton>("X");
    closeBtn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFB91C1C));
    closeBtn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    closeBtn->onClick = [this]() { setVisible(false); if (onClose) onClose(); };
    addAndMakeVisible(closeBtn.get());

    updateDescriptionForSelection();
}

void SetPreparationView::AlgorithmSelector::updateDescriptionForSelection()
{
    if (!algorithmCombo || !descriptionLabel) return;
    int id = algorithmCombo->getSelectedId();
    juce::String desc;
    bool weightsOn = true;
    switch (id)
    {
        case 1:  desc = "Nearest-Neighbor: rapide, qualite moyenne. Recommande pour <30 pistes."; break;
        case 2:  desc = "Optimal exhaustif: meilleure qualite, lent (>10 pistes peut prendre plusieurs secondes)."; break;
        case 3:  desc = "Simulated Annealing: bon compromis qualite/vitesse. Ideal pour 20-100 pistes."; break;
        case 4:  desc = "Pure 2-Opt: optimise en echangeant des segments. Rapide et robuste, ideal >30 pistes."; break;
        case 10: desc = "Quick Auto BPM: progression BPM croissante douce. Poids ignores."; weightsOn = false; break;
        case 11: desc = "Quick Auto Energy: courbe d'energie progressive. Poids ignores."; weightsOn = false; break;
        case 12: desc = "Quick Auto Harmonic: enchainement harmonique Camelot. Poids ignores."; weightsOn = false; break;
        case 13: desc = "Quick Party Mix: high-energy bangers, BPM soutenu. Poids ignores."; weightsOn = false; break;
        case 14: desc = "Quick Chill Session: ambiance posee, energie basse. Poids ignores."; weightsOn = false; break;
        case 15: desc = "Quick Progressive: build-up continu energie + BPM. Poids ignores."; weightsOn = false; break;
        default: desc = "Selectionnez un algorithme."; break;
    }
    descriptionLabel->setText(desc, juce::dontSendNotification);
    setWeightsEnabled(weightsOn);
}

void SetPreparationView::AlgorithmSelector::setWeightsEnabled(bool enabled)
{
    float alpha = enabled ? 1.0f : 0.4f;
    auto applyTo = [alpha, enabled](juce::Component* c) {
        if (!c) return;
        c->setEnabled(enabled);
        c->setAlpha(alpha);
    };
    applyTo(bpmWeight.get());
    applyTo(keyWeight.get());
    applyTo(energyWeight.get());
    applyTo(genreWeight.get());
    applyTo(bpmWLabel.get());
    applyTo(keyWLabel.get());
    applyTo(energyWLabel.get());
    applyTo(genreWLabel.get());
}

void SetPreparationView::AlgorithmSelector::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(Colors::bgCard().withAlpha(0.95f));
    g.fillRoundedRectangle(b, 12.0f);
    g.setColour(Colors::primary().withAlpha(0.5f));
    g.drawRoundedRectangle(b, 12.0f, 1.5f);

    g.setFont(juce::Font("Segoe UI", 13.0f, juce::Font::bold));
    g.setColour(Colors::primary());
    g.drawText("SELECTEUR D'ALGORITHME", 14, 8, 300, 20, juce::Justification::centredLeft);
}

void SetPreparationView::AlgorithmSelector::resized()
{
    if (closeBtn) closeBtn->setBounds(getWidth() - 32, 4, 28, 24);
    auto area = getLocalBounds().reduced(14);
    area.removeFromTop(28);
    algorithmCombo->setBounds(area.removeFromTop(26));
    area.removeFromTop(4);
    if (descriptionLabel)
        descriptionLabel->setBounds(area.removeFromTop(30));
    area.removeFromTop(4);

    auto row = [&](juce::Label* lbl, juce::Slider* sl) {
        auto r = area.removeFromTop(22);
        lbl->setBounds(r.removeFromLeft(50));
        sl->setBounds(r);
        area.removeFromTop(2);
    };
    row(bpmWLabel.get(), bpmWeight.get());
    row(keyWLabel.get(), keyWeight.get());
    row(energyWLabel.get(), energyWeight.get());
    row(genreWLabel.get(), genreWeight.get());

    area.removeFromTop(8);
    auto btnRow = area.removeFromTop(28);
    applyBtn->setBounds(btnRow.removeFromLeft(btnRow.getWidth() / 2 - 4));
    btnRow.removeFromLeft(8);
    cancelBtn->setBounds(btnRow);
}

void SetPreparationView::ValidationWarningsPanel::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    if (trackCount < 2)
    {
        g.setColour(Colors::bgLighter().withAlpha(0.25f));
        g.fillRoundedRectangle(b, 6.0f);
        g.setColour(Colors::textMuted());
        g.setFont(juce::Font(10.0f));
        g.drawText(BM_TJ("setPrep.validation.empty"), b, juce::Justification::centred);
        return;
    }

    if (report.issues.empty())
    {
        g.setColour(Colors::success().withAlpha(0.08f));
        g.fillRoundedRectangle(b, 6.0f);
        g.setColour(Colors::success());
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText("Set valide - aucun probleme detecte", b, juce::Justification::centred);
        return;
    }

    g.setColour(report.errorCount > 0 ? Colors::error().withAlpha(0.08f) : Colors::warning().withAlpha(0.08f));
    g.fillRoundedRectangle(b, 6.0f);

    float y = 4.0f;
    g.setFont(juce::Font(9.0f, juce::Font::bold));

    juce::String summary = juce::String(report.errorCount) + " erreurs, " +
                           juce::String(report.warningCount) + " avertissements";
    g.setColour(report.errorCount > 0 ? Colors::error() : Colors::warning());
    g.drawText(summary, 8, (int)y, (int)b.getWidth() - 16, 12, juce::Justification::centredLeft);
    y += 14;

    g.setFont(juce::Font(8.0f));
    int maxIssues = juce::jmin((int)report.issues.size(), 5);
    for (int i = 0; i < maxIssues; ++i)
    {
        auto& issue = report.issues[static_cast<size_t>(i)];
        juce::Colour col = issue.severity == Services::Preparation::ValidationSeverity::Error
            ? Colors::error() : issue.severity == Services::Preparation::ValidationSeverity::Warning
            ? Colors::warning() : Colors::info();

        juce::String prefix = issue.severity == Services::Preparation::ValidationSeverity::Error ? "ERR" : "WARN";
        g.setColour(col);
        g.drawText(prefix, 8, (int)y, 30, 10, juce::Justification::centredLeft);
        g.setColour(Colors::textSecondary());
        g.drawText(juce::String(issue.message), 40, (int)y, (int)b.getWidth() - 48, 10, juce::Justification::centredLeft, true);
        y += 12;
    }
}

SetPreparationView::PrepStepsBar::PrepStepsBar()
{
    m_ctaBtn = std::make_unique<juce::TextButton>();
    m_ctaBtn->setColour(juce::TextButton::buttonColourId, Colors::modulePrep().withAlpha(0.85f));
    m_ctaBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_ctaBtn->onClick = [this] {
        if (onStepAction) onStepAction(m_current == 0 ? 5 : m_current);
    };
    addAndMakeVisible(*m_ctaBtn);
}

void SetPreparationView::PrepStepsBar::setStates(const std::array<bool, 5>& done)
{
    m_done = done;
    m_current = 0;
    for (int i = 0; i < 5; ++i)
        if (!m_done[(size_t)i]) { m_current = i + 1; break; }
    if (m_ctaBtn)
        m_ctaBtn->setButtonText(m_current == 0 ? doneLabel : ctaLabels[(size_t)(m_current - 1)]);
    repaint();
}

juce::Rectangle<int> SetPreparationView::PrepStepsBar::chipArea(int i) const
{
    const int ctaW = 190;
    const int avail = getWidth() - ctaW - 16;
    const int chipW = juce::jmax(90, avail / 5);
    return { 4 + i * chipW, 0, chipW - 8, getHeight() };
}

void SetPreparationView::PrepStepsBar::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(Colors::bgMedium().withAlpha(0.45f));
    g.fillRoundedRectangle(b, 10.0f);
    g.setColour(Colors::border().withAlpha(0.35f));
    g.drawRoundedRectangle(b.reduced(0.5f), 10.0f, 1.0f);

    const auto accent = Colors::modulePrep();
    const float cy = b.getCentreY();

    for (int i = 0; i < 5; ++i)
    {
        auto area = chipArea(i);
        const bool done    = m_done[(size_t)i];
        const bool current = (m_current == i + 1);
        const bool hover   = (m_hover == i);

        if (i < 4)
        {
            auto next = chipArea(i + 1);
            g.setColour((done ? accent : Colors::border()).withAlpha(done ? 0.55f : 0.35f));
            g.fillRect((float)(area.getRight() - 2), cy - 0.75f,
                       (float)(next.getX() - area.getRight() + 6), 1.5f);
        }

        const float r = 11.0f;
        const float cx = (float)area.getX() + 14.0f + r;
        juce::Rectangle<float> circle(cx - r, cy - r, r * 2.0f, r * 2.0f);

        if (current)
        {
            g.setColour(accent.withAlpha(0.18f));
            g.fillEllipse(circle.expanded(5.0f));
            g.setColour(accent);
            g.fillEllipse(circle);
        }
        else if (done)
        {
            g.setColour(accent.withAlpha(0.22f));
            g.fillEllipse(circle);
            g.setColour(accent.withAlpha(0.9f));
            g.drawEllipse(circle, 1.4f);
        }
        else
        {
            g.setColour(Colors::bgLighter().withAlpha(hover ? 0.9f : 0.6f));
            g.fillEllipse(circle);
            g.setColour(Colors::border().withAlpha(0.7f));
            g.drawEllipse(circle, 1.0f);
        }

        if (done && !current)
        {
            juce::Path check;
            check.startNewSubPath(cx - 4.5f, cy + 0.5f);
            check.lineTo(cx - 1.0f, cy + 4.0f);
            check.lineTo(cx + 5.0f, cy - 4.0f);
            g.setColour(accent);
            g.strokePath(check, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }
        else
        {
            g.setColour(current ? Colors::textPrimary()
                                : done ? accent : Colors::textMuted());
            g.setFont(juce::Font(12.0f, juce::Font::bold));
            g.drawText(juce::String(i + 1), circle.toNearestInt(), juce::Justification::centred);
        }

        g.setColour(current ? Colors::textPrimary()
                            : done ? Colors::textSecondary() : Colors::textMuted());
        g.setFont(juce::Font(12.0f, current ? juce::Font::bold : juce::Font::plain));
        g.drawText(stepLabels[(size_t)i],
                   area.getX() + 14 + (int)(r * 2.0f) + 8, 0,
                   area.getWidth() - 14 - (int)(r * 2.0f) - 10, getHeight(),
                   juce::Justification::centredLeft);
    }
}

void SetPreparationView::PrepStepsBar::resized()
{
    if (m_ctaBtn)
        m_ctaBtn->setBounds(getWidth() - 190 - 8, (getHeight() - 28) / 2, 190, 28);
}

void SetPreparationView::PrepStepsBar::mouseUp(const juce::MouseEvent& e)
{
    for (int i = 0; i < 5; ++i)
        if (chipArea(i).contains(e.getPosition()))
        {
            if (onStepAction) onStepAction(i + 1);
            return;
        }
}

void SetPreparationView::PrepStepsBar::mouseMove(const juce::MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < 5; ++i)
        if (chipArea(i).contains(e.getPosition())) { h = i; break; }
    if (h != m_hover) { m_hover = h; repaint(); }
}

void SetPreparationView::DragDropSetlistModel::paintListBoxItem(
    int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (!tracks || row < 0 || row >= (int)tracks->size()) return;
    const auto& t = (*tracks)[row];

    int trackH = 36;
    int badgeH = h - trackH;

    if (selected)
    {
        g.setColour(Colors::primary().withAlpha(0.18f));
        g.fillRect(0, 0, w, trackH);
    }
    else if (row % 2 == 0)
    {
        g.setColour(Colors::bgDark().withAlpha(0.4f));
        g.fillRect(0, 0, w, trackH);
    }

    int x = 10;

    g.setColour(Colors::textSecondary().withAlpha(0.7f));
    for (int li = 0; li < 3; ++li)
        g.fillRect(x, trackH / 2 - 4 + li * 4, 12, 2);
    x += 18;

    {
        float circleSize = 22.0f;
        float cx = (float)x + circleSize * 0.5f;
        float cy = trackH * 0.5f;
        g.setColour(Colors::primary().withAlpha(0.2f));
        g.fillEllipse(cx - circleSize * 0.5f, cy - circleSize * 0.5f, circleSize, circleSize);
        g.setColour(Colors::primary());
        g.setFont(juce::Font("Segoe UI", 11.0f, juce::Font::bold));
        g.drawText(juce::String(row + 1), (int)(cx - 11), 0, 22, trackH, juce::Justification::centred);
    }
    x += 26;

    g.setColour(Colors::textPrimary());
    g.setFont(juce::Font("Segoe UI", 12.0f, juce::Font::bold));
    g.drawText(juce::String(t.title), x, 0, 180, trackH, juce::Justification::centredLeft);
    x += 184;

    g.setColour(Colors::textSecondary());
    g.setFont(juce::Font("Segoe UI", 11.0f, juce::Font::plain));
    g.drawText(juce::String(t.artist), x, 0, 120, trackH, juce::Justification::centredLeft);
    x += 124;

    ProDraw::badge(g, t.bpm > 0 ? juce::String(t.bpm, 1) : "-", (float)x, 6.0f, 56.0f, 24.0f, Colors::bpmBadge());
    x += 60;

    auto keyStr = t.camelotKey.empty() ? juce::String(t.key) : juce::String(t.camelotKey);
    ProDraw::badge(g, keyStr, (float)x, 6.0f, 46.0f, 24.0f, Colors::keyBadge());
    x += 50;

    {
        float eNorm = t.energy / 10.0f;
        int barW = 54;
        float barH = 8.0f;
        float barY = (float)(trackH / 2 - 4);
        g.setColour(Colors::bgLightest().withAlpha(0.6f));
        g.fillRoundedRectangle((float)x, barY, (float)barW, barH, 4.0f);
        juce::Colour eCol = eNorm < 0.35f ? Colors::success() : eNorm < 0.65f ? Colors::warning() : Colors::error();
        g.setColour(eCol);
        g.fillRoundedRectangle((float)x, barY, barW * eNorm, barH, 4.0f);
        g.setFont(juce::Font("Segoe UI", 9.0f, juce::Font::bold));
        g.setColour(eCol);
        g.drawText(t.energy > 0 ? juce::String((int)t.energy) : "-", x + barW + 4, 0, 18, trackH, juce::Justification::centredLeft);
    }
    x += 78;

    int mins = (int)(t.duration / 60.0);
    int secs = (int)t.duration % 60;
    g.setColour(Colors::textMuted());
    g.setFont(juce::Font("Segoe UI", 10.0f, juce::Font::plain));
    g.drawText(juce::String::formatted("%d:%02d", mins, secs), x, 0, 44, trackH, juce::Justification::centred);

    if (badgeH > 4 && transitions && row < (int)transitions->size())
    {
        const auto& ti = (*transitions)[row];

        juce::Rectangle<float> stripBg(24.0f, (float)(trackH + 1), (float)(w - 48), (float)(badgeH - 2));
        g.setColour(Colors::bgLightest().withAlpha(0.55f));
        g.fillRoundedRectangle(stripBg, 4.0f);
        g.setColour(ti.color.withAlpha(0.16f));
        g.fillRoundedRectangle(stripBg, 4.0f);
        g.setColour(ti.color.withAlpha(0.8f));
        g.fillRect(24, trackH + 1, 3, badgeH - 2);

        float dotX = 38.0f;
        float dotY = trackH + badgeH / 2.0f;
        g.setColour(ti.color.withAlpha(0.25f));
        g.fillEllipse(dotX - 8.0f, dotY - 8.0f, 16.0f, 16.0f);
        g.setColour(ti.color);
        g.fillEllipse(dotX - 4.0f, dotY - 4.0f, 8.0f, 8.0f);

        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.setColour(ti.color);
        g.drawText(juce::String(ti.scorePercent) + "%", 48, trackH, 46, badgeH, juce::Justification::centredLeft);

        g.setFont(juce::Font(9.0f, juce::Font::bold));
        const bool bpmMatch = std::abs(ti.bpmDiff) < 0.1f;
        juce::String bpmText = bpmMatch ? "MATCH"
            : ((ti.bpmDiff >= 0 ? "+" : "") + juce::String(ti.bpmDiff, 1) + " BPM");
        g.setColour(bpmMatch ? Colors::success() : Colors::bpmBadge());
        g.drawText(bpmText, 98, trackH, 80, badgeH, juce::Justification::centredLeft);

        juce::String keyText = ti.keyCompat ? "Harmonique" : "Clash";
        juce::Colour keyBg = ti.keyCompat ? Colors::success() : Colors::error();
        ProDraw::badge(g, keyText, 182.0f, (float)(trackH + 2), 72.0f, (float)(badgeH - 4), keyBg);

        g.setFont(juce::Font(8.5f));
        g.setColour(ti.keyCompat ? Colors::keyBadge().brighter(0.3f) : Colors::warning());
        g.drawText(ti.transitionType, 260, trackH, 200, badgeH, juce::Justification::centredLeft);
    }

    g.setColour(Colors::border().withAlpha(0.15f));
    g.drawHorizontalLine(h - 1, 0.0f, (float)w);
}

juce::var SetPreparationView::DragDropSetlistModel::getDragSourceDescription(const juce::SparseSet<int>& selectedRows)
{
    if (selectedRows.size() <= 0) return juce::var(-1);
    juce::String s = "BEATMATE_ROWS:";
    for (int i = 0; i < selectedRows.size(); ++i)
    {
        if (i > 0) s << ",";
        s << selectedRows[i];
    }
    return juce::var(s);
}

void SetPreparationView::DragDropSetlistModel::itemDropped(const juce::DragAndDropTarget::SourceDetails& details)
{
    // Drop handling lives in DragDropListBox::itemDropped.
    juce::ignoreUnused(details);
    dragInsertIndex = -1;
}

void SetPreparationView::DragDropListBox::mouseDrag(const juce::MouseEvent& e)
{
    juce::ListBox::mouseDrag(e);

    if (e.getDistanceFromDragStart() > 5)
    {
        int row = getRowContainingPosition(e.x, e.y);
        if (row >= 0)
        {
            dragSourceRow = row;
            auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
            if (container && !container->isDragAndDropActive())
            {
                // If multiple rows are selected, ship them all as "BEATMATE_ROWS:1,4,7"
                auto sel = getSelectedRows();
                if (sel.size() > 1)
                {
                    juce::String s = "BEATMATE_ROWS:";
                    for (int i = 0; i < sel.size(); ++i)
                    {
                        if (i > 0) s << ",";
                        s << sel[i];
                    }
                    container->startDragging(juce::var(s), this);
                }
                else
                {
                    container->startDragging(juce::var(row), this);
                }
            }
        }
    }
}

void SetPreparationView::DragDropListBox::itemDragMove(const juce::DragAndDropTarget::SourceDetails& details)
{
    int insertRow = getInsertionIndexForPosition(details.localPosition.x, details.localPosition.y);
    if (insertRow != dragInsertIndex)
    {
        dragInsertIndex = insertRow;
        repaint();
    }
}

void SetPreparationView::DragDropListBox::itemDragExit(const juce::DragAndDropTarget::SourceDetails&)
{
    dragInsertIndex = -1;
    repaint();
}

void SetPreparationView::DragDropListBox::itemDropped(const juce::DragAndDropTarget::SourceDetails& details)
{
    int toRow = getInsertionIndexForPosition(details.localPosition.x, details.localPosition.y);
    if (toRow < 0) toRow = dragInsertIndex;

    if (details.description.isString())
    {
        juce::String desc = details.description.toString();
        if (desc.startsWith("BEATMATE_TRACKS:") && onExternalDrop)
        {
            // Stash insertion index for the bound onExternalDrop -> handleLibraryDrop chain
            externalDropInsertIndex = toRow;
            onExternalDrop(desc);
            externalDropInsertIndex = -1;
            dragInsertIndex = -1;
            dragSourceRow = -1;
            repaint();
            return;
        }

        // Multi-row internal reorder: "BEATMATE_ROWS:1,4,7"
        if (desc.startsWith("BEATMATE_ROWS:") && onReorderMulti)
        {
            juce::String idsPart = desc.fromFirstOccurrenceOf("BEATMATE_ROWS:", false, false);
            juce::StringArray toks;
            toks.addTokens(idsPart, ",", "");
            std::vector<int> rows;
            rows.reserve((size_t)toks.size());
            for (auto& t : toks) { int v = t.getIntValue(); if (v >= 0) rows.push_back(v); }
            dragInsertIndex = -1;
            dragSourceRow = -1;
            if (!rows.empty() && toRow >= 0)
                onReorderMulti(rows, toRow);
            repaint();
            return;
        }
    }

    // Single-row internal reorder (legacy int payload)
    int fromRow = details.description.isInt() ? (int)details.description : -1;
    dragInsertIndex = -1;
    dragSourceRow = -1;

    if (fromRow >= 0 && toRow >= 0 && fromRow != toRow && onReorder)
        onReorder(fromRow, toRow);
    repaint();
}

void SetPreparationView::DragDropListBox::paint(juce::Graphics& g)
{
    juce::ListBox::paint(g);

    if (dragInsertIndex >= 0)
    {
        int rowH = getRowHeight();
        int y = dragInsertIndex * rowH - getViewport()->getViewPositionY();
        g.setColour(Colors::primary().withAlpha(0.8f));
        g.fillRect(0, y - 1, getWidth(), 3);
    }
}

struct SetPreparationView::LibBrowserListener : public LibraryBrowserPanel::Listener
{
    SetPreparationView* owner;
    LibBrowserListener(SetPreparationView* o) : owner(o) {}

    void trackDoubleClicked(const Models::Track& track) override
    {
        owner->addTrackToSet(track);
    }

    void addTrackRequested(const Models::Track& track) override
    {
        owner->addTrackToSet(track);
    }
};

SetPreparationView::~SetPreparationView()
{
    stopTimer();
    if (m_persistDirty)
    {
        m_persistDirty = false;
        saveSetToJSON();
    }

    // Remove listener from library browser before both are destroyed, to avoid dangling pointer.
    if (m_libraryBrowser && m_libBrowserListener)
        m_libraryBrowser->removeListener(m_libBrowserListener.get());
}

SetPreparationView::SetPreparationView()
    : m_provider(nullptr)
{
    setupUI();

    m_setlistModel->tracks = &m_tracks;
    m_setlistModel->transitions = &m_transitionInfos;

    loadSetFromJSON();
    updateHeaderSubtitle();
    updateCompatibility();
    updateTimer();
    updateSetScore();
    retranslateUi();
}

SetPreparationView::SetPreparationView(Services::Library::TrackDataProvider* provider)
    : m_provider(provider)
{
    setupUI();

    m_setlistModel->tracks = &m_tracks;
    m_setlistModel->transitions = &m_transitionInfos;

    if (m_provider)
    {
        juce::Component::SafePointer<SetPreparationView> self(this);
        m_provider->onDataChanged([self] {
            juce::MessageManager::callAsync([self]() {
                if (!self) return;
                if (self->m_libraryBrowser) self->m_libraryBrowser->refreshResults();
            });
        });
    }

    loadSetFromJSON();
    updateHeaderSubtitle();
    updateCompatibility();
    updateTimer();
    updateSetScore();
    retranslateUi();
}

void SetPreparationView::setupUI()
{
    m_titleLabel = std::make_unique<juce::Label>("t", BM_TJ("setPrep.title"));
    m_titleLabel->setFont(juce::Font("Segoe UI", 22.0f, juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId, Colors::primary());

    m_subtitleLabel = std::make_unique<juce::Label>("sub", "Preparation de set intelligente avec bibliotheque integree");
    m_subtitleLabel->setFont(juce::Font("Segoe UI", 12.0f, juce::Font::plain));
    m_subtitleLabel->setColour(juce::Label::textColourId, Colors::textMuted());
    addAndMakeVisible(*m_subtitleLabel);
    addAndMakeVisible(*m_titleLabel);

    auto makeStatLabel = [this](const juce::String& text, juce::Colour col, float fontSize = 14.0f)
    {
        auto l = std::make_unique<juce::Label>("", text);
        l->setFont(juce::Font("Segoe UI", fontSize, juce::Font::bold));
        l->setColour(juce::Label::textColourId, col);
        addAndMakeVisible(*l);
        return l;
    };

    m_trackCountLabel    = makeStatLabel(BM_TJ("setPrep.label.tracks") + " 0", Colors::primary(), 16.0f);
    m_totalDurationLabel = makeStatLabel(BM_TJ("setPrep.label.duration") + " 0:00:00", Colors::accent(), 14.0f);
    m_avgBPMLabel        = makeStatLabel(BM_TJ("setPrep.label.avgBPM") + " -", Colors::warning(), 14.0f);
    m_compatibilityLabel = makeStatLabel(BM_TJ("setPrep.label.compatibility") + " -", juce::Colour(0xFFFF6B9D), 14.0f);
    m_timerLabel         = makeStatLabel(BM_TJ("setPrep.label.timer") + " 0h 00m", Colors::textSecondary(), 13.0f);

    m_setDuration = std::make_unique<juce::Slider>(juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft);
    m_setDuration->setRange(15, 600, 5);
    m_setDuration->setValue(Prefs::getInt("setPrep.setDurationMin", 120));
    m_setDuration->setTextValueSuffix(" min");
    m_setDuration->setColour(juce::Slider::textBoxTextColourId, Colors::textPrimary());
    m_setDuration->setColour(juce::Slider::textBoxBackgroundColourId, Colors::bgLighter());
    m_setDuration->setColour(juce::Slider::textBoxOutlineColourId, Colors::border());
    m_setDuration->onValueChange = [this] {
        if (m_timerLabel)
        {
            int mins = (int)m_setDuration->getValue();
            m_timerLabel->setText(BM_TJ("setPrep.label.target") + " " + juce::String(mins / 60) + "h " +
                                  juce::String::formatted("%02d", mins % 60) + "m",
                                  juce::dontSendNotification);
        }
        if (m_setTimeline)
        {
            m_setTimeline->targetDurationMin = m_setDuration->getValue();
            m_setTimeline->repaint();
        }
        Prefs::setInt("setPrep.setDurationMin", (int)m_setDuration->getValue());
    };
    addAndMakeVisible(*m_setDuration);

    auto makeBtn = [this](const juce::String& text, juce::Colour bg = Colors::bgLighter())
    {
        auto b = std::make_unique<juce::TextButton>(text);
        b->setColour(juce::TextButton::buttonColourId, bg);
        b->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        addAndMakeVisible(*b);
        return b;
    };

    m_iaAutoArrangeBtn = makeBtn(BM_TJ("setPrep.btn.aiArrange"), juce::Colour(0xFF2563EB));
    m_iaAutoArrangeBtn->setTooltip(BM_TJ("setPrep.tip.aiArrange"));
    m_sortBPMBtn    = makeBtn("Trier BPM", Colors::bpmBadge());
    m_sortKeyBtn    = makeBtn("Trier Key", Colors::keyBadge());
    m_sortEnergyBtn = makeBtn("Trier Energy", Colors::energyBadge());
    m_sortBPMBtn->setVisible(false);
    m_sortKeyBtn->setVisible(false);
    m_sortEnergyBtn->setVisible(false);
    m_sortMenuBtn = makeBtn(BM_TJ("setPrep.btn.sort") + juce::String::fromUTF8(" \xe2\x96\xbe"));
    m_addBtn    = makeBtn("+ " + BM_TJ("common.add"));
    m_removeBtn = makeBtn(BM_TJ("setPrep.remove"));
    m_clearBtn = makeBtn(BM_TJ("setPrep.clear"), Colors::error().withAlpha(0.7f));
    m_clearBtn->onClick = [this] { onClearSet(); };
    m_checkCompatBtn = makeBtn(BM_TJ("setPrep.btn.check"), Colors::accent().withAlpha(0.6f));
    m_exportBtn      = makeBtn(BM_TJ("setPrep.export"), Colors::primary());

    m_moveUpBtn     = makeBtn(BM_TJ("setPrep.moveUp"));
    m_moveDownBtn   = makeBtn(BM_TJ("setPrep.moveDown"));
    m_autoSortBtn   = makeBtn("Auto-Sort", Colors::primary());
    m_fillGapsBtn   = makeBtn("Fill Gaps", Colors::accent());
    m_optimizeEnergyBtn = makeBtn("Optimize Energy", Colors::warning().withAlpha(0.7f));
    m_randomizeBtn  = makeBtn("Randomize");

    m_moveUpBtn->setVisible(false);
    m_moveDownBtn->setVisible(false);
    m_autoSortBtn->setVisible(false);
    m_fillGapsBtn->setVisible(false);
    m_optimizeEnergyBtn->setVisible(false);
    m_randomizeBtn->setVisible(false);

    m_iaAutoArrangeBtn->onClick = [this] { spdlog::info("[SetPreparationView] IA AutoArrange clicked"); onIAAutoArrange(); };
    m_sortBPMBtn->onClick    = [this] { spdlog::info("[SetPreparationView] sortBPM clicked"); onSortByBPM(); };
    m_sortKeyBtn->onClick    = [this] { spdlog::info("[SetPreparationView] sortKey clicked"); onSortByKey(); };
    m_sortEnergyBtn->onClick = [this] { spdlog::info("[SetPreparationView] sortEnergy clicked"); onSortByEnergy(); };
    m_sortMenuBtn->onClick = [this] {
        juce::PopupMenu menu;
        menu.addItem(1, BM_TJ("setPrep.sort.bpm"));
        menu.addItem(2, BM_TJ("setPrep.sort.key"));
        menu.addItem(3, BM_TJ("setPrep.sort.energy"));
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(m_sortMenuBtn.get()),
            [this](int result) {
                if (result == 1) onSortByBPM();
                else if (result == 2) onSortByKey();
                else if (result == 3) onSortByEnergy();
            });
    };
    m_addBtn->onClick    = [this] { spdlog::info("[SetPreparationView] addTrack clicked"); onAddTrack(); };
    m_removeBtn->onClick = [this] { spdlog::info("[SetPreparationView] removeTrack clicked"); onRemoveTrack(); };
    m_checkCompatBtn->onClick = [this] { spdlog::info("[SetPreparationView] checkCompatibility clicked"); onCheckCompatibility(); };
    m_exportBtn->onClick = [this] { spdlog::info("[SetPreparationView] exportSetlist clicked"); onExportSetlist(); };

    m_moveUpBtn->onClick = [this] { spdlog::info("[SetPreparationView] moveUp clicked"); onMoveUp(); };
    m_moveDownBtn->onClick = [this] { spdlog::info("[SetPreparationView] moveDown clicked"); onMoveDown(); };
    m_autoSortBtn->onClick = [this] { spdlog::info("[SetPreparationView] autoSort clicked"); onAutoSort(); };
    m_fillGapsBtn->onClick = [this] { spdlog::info("[SetPreparationView] fillGaps clicked"); onFillGaps(); };
    m_optimizeEnergyBtn->onClick = [this] { spdlog::info("[SetPreparationView] optimizeEnergy clicked"); onOptimizeEnergy(); };
    m_randomizeBtn->onClick = [this] { spdlog::info("[SetPreparationView] randomize clicked"); onRandomize(); };

    m_setlistModel = std::make_unique<DragDropSetlistModel>();
    m_setlistTable = std::make_unique<DragDropListBox>("sl", m_setlistModel.get());
    m_setlistTable->setRowHeight(56);
    m_setlistTable->setColour(juce::ListBox::backgroundColourId, Colors::bgDarker());
    m_setlistTable->setColour(juce::ListBox::outlineColourId, Colors::border());
    m_setlistTable->setMultipleSelectionEnabled(true);
    addAndMakeVisible(*m_setlistTable);

    m_setlistTable->onReorder = [this](int from, int to) {
        if (from < 0 || from >= (int)m_tracks.size()) return;
        if (to < 0) to = 0;
        if (to > (int)m_tracks.size()) to = (int)m_tracks.size();

        auto item = m_tracks[from];
        m_tracks.erase(m_tracks.begin() + from);
        int insertAt = (to > from) ? to - 1 : to;
        if (insertAt < 0) insertAt = 0;
        if (insertAt > (int)m_tracks.size()) insertAt = (int)m_tracks.size();
        m_tracks.insert(m_tracks.begin() + insertAt, item);

        m_hasArranged = true;
        m_setlistTable->updateContent();
        m_setlistTable->selectRow(insertAt);
        updateCompatibility();
        updateSetScore();
        refreshStatistics();
    };

    m_setlistTable->onReorderMulti = [this](const std::vector<int>& fromRows, int to) {
        if (fromRows.empty() || m_tracks.empty()) return;
        std::vector<int> rows = fromRows;
        std::sort(rows.begin(), rows.end());
        rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

        std::vector<Models::Track> moving;
        moving.reserve(rows.size());
        for (int r : rows)
            if (r >= 0 && r < (int)m_tracks.size()) moving.push_back(m_tracks[r]);

        // Number of selected rows strictly before `to` — adjust insert index after erasures.
        int adjust = 0;
        for (int r : rows) if (r < to) ++adjust;

        // Erase from highest to lowest to keep indices valid.
        for (auto it = rows.rbegin(); it != rows.rend(); ++it)
            if (*it >= 0 && *it < (int)m_tracks.size())
                m_tracks.erase(m_tracks.begin() + *it);

        int insertAt = juce::jlimit(0, (int)m_tracks.size(), to - adjust);
        m_tracks.insert(m_tracks.begin() + insertAt, moving.begin(), moving.end());

        m_hasArranged = true;
        m_setlistTable->updateContent();
        m_setlistTable->selectRangeOfRows(insertAt, insertAt + (int)moving.size() - 1);
        updateCompatibility();
        updateSetScore();
        refreshStatistics();
    };

    m_setlistTable->onExternalDrop = [this](const juce::String& trackIdsStr) {
        int insertIdx = m_setlistTable ? m_setlistTable->externalDropInsertIndex : -1;
        handleLibraryDrop(trackIdsStr, insertIdx);
    };

    m_setlistModel->onDoubleClick = [this](int row) {
        if (row >= 0 && row < (int)m_tracks.size())
        {
            auto& trk = m_tracks[row];
            if (onTrackPreview)
                onTrackPreview(juce::String(trk.filePath), juce::String(trk.title), juce::String(trk.artist));
            m_listeners.call([&](Listener& l) {
                l.trackPreviewRequested(juce::String(trk.filePath), juce::String(trk.title), juce::String(trk.artist));
            });
        }
    };

    m_setlistModel->onTransitionClicked = [this](int idx) {
        if (idx >= 0 && idx < (int)m_tracks.size() - 1 && m_transitionEditor)
        {
            auto result = m_matchUp.matchTracks(m_tracks[idx], m_tracks[idx + 1]);
            const int rowScore = idx < (int)m_transitionInfos.size()
                ? m_transitionInfos[(size_t)idx].scorePercent : -1;
            m_transitionEditor->updateFromResult(result, idx, rowScore);
            m_transitionEditor->setVisible(true);
            resized();
        }
    };

    m_energyCurve = std::make_unique<EnergyCurveGraph>();
    addAndMakeVisible(*m_energyCurve);

    m_setScore = std::make_unique<SetScoreCircle>();
    m_setScore->setTooltip(BM_TJ("setPrep.tip.score"));
    addAndMakeVisible(*m_setScore);


    // Library Browser : monte dans une fenetre dediee (lancee par onBrowseLibrary),
    m_libraryBrowser = std::make_unique<LibraryBrowserPanel>(m_provider);
    m_libBrowserListener = std::make_unique<LibBrowserListener>(this);
    m_libraryBrowser->addListener(m_libBrowserListener.get());
    m_libraryBrowser->setVisible(false);

    m_statsDashboard = std::make_unique<StatisticsDashboard>();
    addAndMakeVisible(*m_statsDashboard);

    m_transitionEditor = std::make_unique<TransitionDetailEditor>();
    m_transitionEditor->setVisible(false);
    addChildComponent(*m_transitionEditor);

    m_algoSelector = std::make_unique<AlgorithmSelector>();
    m_algoSelector->setVisible(false);
    m_algoSelector->onApply = [this](int algo, float bW, float kW, float eW, float gW, int quickMode) {
        m_algoSelector->setVisible(false);
        resized();
        runAutoArrange(algo, bW, kW, eW, gW, quickMode);
    };
    m_algoSelector->onClose = [this] {
        if (m_algoSelector) m_algoSelector->setVisible(false);
        resized();
        repaint();
    };
    addChildComponent(*m_algoSelector);

    m_validationPanel = std::make_unique<ValidationWarningsPanel>();
    addAndMakeVisible(*m_validationPanel);

    m_energyCurveEditor = std::make_unique<Widgets::EnergyCurveEditor>();
    addAndMakeVisible(*m_energyCurveEditor);

    m_bpmGraph = std::make_unique<Widgets::BPMProgressionGraph>();
    addAndMakeVisible(*m_bpmGraph);

    m_relatedTracks = std::make_unique<Widgets::RelatedTracksPanel>(m_provider);
    addAndMakeVisible(*m_relatedTracks);

    auto makeInfoLabel = [this](const juce::String& text)
    {
        auto l = std::make_unique<juce::Label>("", text);
        l->setFont(juce::Font(11.0f));
        l->setColour(juce::Label::textColourId, Colors::textMuted());
        addAndMakeVisible(*l);
        return l;
    };
    auto makeInfoEditor = [this](const juce::String& placeholder)
    {
        auto e = std::make_unique<juce::TextEditor>();
        e->setColour(juce::TextEditor::backgroundColourId, Colors::bgLighter().withAlpha(0.5f));
        e->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
        e->setColour(juce::TextEditor::outlineColourId, Colors::border().withAlpha(0.4f));
        e->setTextToShowWhenEmpty(placeholder, Colors::textDim());
        addAndMakeVisible(*e);
        return e;
    };

    m_setNameLabel  = makeInfoLabel(juce::String::fromUTF8("Nom:"));
    m_djLabel       = makeInfoLabel(juce::String::fromUTF8("DJ:"));
    m_venueLabel    = makeInfoLabel(juce::String::fromUTF8("Lieu:"));
    m_dateLabel     = makeInfoLabel(juce::String::fromUTF8("Date:"));
    m_durationLabel = makeInfoLabel(juce::String::fromUTF8("Duree:"));

    m_setNameEditor = makeInfoEditor(juce::String::fromUTF8("Mon set"));
    m_djEditor      = makeInfoEditor(juce::String::fromUTF8("Nom du DJ"));
    m_venueEditor   = makeInfoEditor(juce::String::fromUTF8("Club / Soiree"));
    m_dateEditor    = makeInfoEditor(juce::Time::getCurrentTime().formatted("%Y-%m-%d"));

    m_setNameEditor->setText(m_eventName, false);
    m_djEditor->setText(m_djName, false);
    m_venueEditor->setText(m_eventVenue, false);
    m_dateEditor->setText(m_eventDate.isNotEmpty() ? m_eventDate
                                                   : juce::Time::getCurrentTime().formatted("%Y-%m-%d"), false);

    m_setNameEditor->onTextChange = [this] { m_eventName  = m_setNameEditor->getText(); updateHeaderSubtitle(); saveSetToJSON(); };
    m_djEditor->onTextChange      = [this] { m_djName     = m_djEditor->getText();      updateHeaderSubtitle(); saveSetToJSON(); };
    m_venueEditor->onTextChange   = [this] { m_eventVenue = m_venueEditor->getText();   updateHeaderSubtitle(); saveSetToJSON(); };
    m_dateEditor->onTextChange    = [this] { m_eventDate  = m_dateEditor->getText();    updateHeaderSubtitle(); saveSetToJSON(); };

    m_profileLabel = makeInfoLabel(juce::String::fromUTF8("Profil energie:"));
    m_profileCombo = std::make_unique<juce::ComboBox>("setProfile");
    m_profileCombo->addItem(juce::String::fromUTF8("Classic DJ Set"), 1);
    m_profileCombo->addItem(juce::String::fromUTF8("Double-drop"), 2);
    m_profileCombo->addItem(juce::String::fromUTF8("Progressive"), 3);
    m_profileCombo->addItem(juce::String::fromUTF8("Marathon"), 4);
    m_profileCombo->addItem(juce::String::fromUTF8("Warm-up / Chill"), 5);
    m_profileCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter().withAlpha(0.5f));
    m_profileCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_profileCombo->setColour(juce::ComboBox::outlineColourId, Colors::border().withAlpha(0.4f));
    m_profileCombo->setTextWhenNothingSelected(BM_TJ("setPrep.profile.hint"));
    m_profileCombo->setTooltip(juce::String::fromUTF8("Reordonne le set selon une courbe d'energie type"));
    m_profileCombo->onChange = [this] {
        int idx = m_profileCombo->getSelectedId() - 1;
        if (idx >= 0) applyEnergyProfile(idx);
        Prefs::setInt("setPrep.energyProfileId", m_profileCombo->getSelectedId());
    };
    addAndMakeVisible(*m_profileCombo);

    m_energyPresets = {
        {"Classic DJ Set",  {3, 5, 7, 9, 7, 4}},
        {"Double-drop",     {4, 8, 5, 9, 6, 3}},
        {"Progressive",     {2, 3, 5, 6, 8, 10}},
        {"Marathon",        {5, 6, 7, 8, 8, 9, 8, 7, 5}},
        {"Warm-up / Chill", {2, 3, 4, 5, 4, 3}},
    };

    m_setTimeline = std::make_unique<SetTimelineComponent>();
    m_setTimeline->tracks = &m_tracks;
    m_setTimeline->onTrackSelected = [this](int idx) {
        if (m_setlistTable && idx >= 0 && idx < (int)m_tracks.size())
        {
            m_setlistTable->selectRow(idx);
            m_setlistTable->scrollToEnsureRowIsOnscreen(idx);
        }
    };
    addAndMakeVisible(*m_setTimeline);

    m_autoFillBtn = makeBtn(BM_TJ("setPrep.btn.aiFill"), Colors::secondary());
    m_autoFillBtn->setTooltip(BM_TJ("setPrep.tip.aiFill"));
    m_autoFillBtn->onClick = [this] { spdlog::info("[SetPreparationView] autoFillAI clicked"); showAutoFillMenu(); };

    m_browseLibraryBtn = makeBtn(BM_TJ("setPrep.btn.library"), Colors::modulePrep().withAlpha(0.85f));
    m_browseLibraryBtn->onClick = [this] { spdlog::info("[SetPreparationView] browseLibrary clicked"); onBrowseLibrary(); };

    m_emptyBrowseBtn = makeBtn(BM_TJ("setPrep.btn.library"), Colors::modulePrep().withAlpha(0.85f));
    m_emptyBrowseBtn->onClick = [this] { onBrowseLibrary(); };
    m_emptyBrowseBtn->setVisible(false);
    m_emptyAutoFillBtn = makeBtn(BM_TJ("setPrep.btn.aiFill"), Colors::secondary());
    m_emptyAutoFillBtn->setTooltip(BM_TJ("setPrep.tip.aiFill"));
    m_emptyAutoFillBtn->onClick = [this] { showAutoFillMenu(); };
    m_emptyAutoFillBtn->setVisible(false);

    m_stepsBar = std::make_unique<PrepStepsBar>();
    refreshStepsBarTexts();
    m_stepsBar->onStepAction = [this](int step) { runStepAction(step); };
    addAndMakeVisible(*m_stepsBar);

    loadChecklistState();
    m_checklist = std::make_unique<ChecklistComponent>();
    buildChecklistItems();
    m_checklist->onChecklistChanged = [this] { saveChecklistState(); };
    addAndMakeVisible(*m_checklist);

    m_exportArchiveBtn = makeBtn(juce::String::fromUTF8("Archiver (.bmset)"), Colors::primary());
    m_exportDJBtn      = makeBtn(juce::String::fromUTF8("Envoyer a un logiciel DJ"), Colors::accent().withAlpha(0.7f));
    m_exportUsbBtn     = makeBtn(juce::String::fromUTF8("Playlist USB (M3U)"), Colors::bgLighter());
    m_exportPrintBtn   = makeBtn(juce::String::fromUTF8("Imprimer / Partager"), Colors::bgLighter());

    m_exportArchiveBtn->onClick = [this] { spdlog::info("[SetPreparationView] export archive"); onArchiveBmset(); };
    m_exportDJBtn->onClick      = [this] { spdlog::info("[SetPreparationView] export to DJ");   onSendToDJSoftware(); };
    m_exportUsbBtn->onClick     = [this] { spdlog::info("[SetPreparationView] export USB");      onExportUsbPlaylist(); };
    m_exportPrintBtn->onClick   = [this] { spdlog::info("[SetPreparationView] export print");    onPrintShare(); };

    const int profId = Prefs::getInt("setPrep.energyProfileId", 0);
    if (profId >= 1 && profId <= m_profileCombo->getNumItems())
        m_profileCombo->setSelectedId(profId, juce::dontSendNotification);
}

void SetPreparationView::retranslateUi()
{
    auto rebuild = [](juce::ComboBox* cb, std::initializer_list<const char*> keys) {
        if (!cb) return;
        int prev = cb->getSelectedId();
        cb->clear(juce::dontSendNotification);
        int id = 1;
        for (auto k : keys) cb->addItem(BM_TJ(k), id++);
        if (prev > 0)
            cb->setSelectedId(prev, juce::dontSendNotification);
    };

    if (m_titleLabel)
        m_titleLabel->setText(BM_TJ("setPrep.title"), juce::dontSendNotification);

    if (m_trackCountLabel)
        m_trackCountLabel->setText(BM_TJ("setPrep.label.tracks") + " " + juce::String((int)m_tracks.size()),
                                   juce::dontSendNotification);

    double totalSec = 0.0;
    float  totalBPM = 0.0f;
    for (auto& t : m_tracks) { totalSec += t.duration; totalBPM += (float)t.bpm; }
    const int hours = (int)(totalSec / 3600);
    const int mins  = (int)(totalSec / 60) % 60;
    const int secs  = (int)totalSec % 60;

    if (m_totalDurationLabel)
        m_totalDurationLabel->setText(BM_TJ("setPrep.label.duration") + " " +
                                      juce::String::formatted("%d:%02d:%02d", hours, mins, secs),
                                      juce::dontSendNotification);
    if (m_timerLabel)
        m_timerLabel->setText(BM_TJ("setPrep.label.timer") + " " + juce::String(hours) + "h " +
                              juce::String::formatted("%02d", mins) + "m",
                              juce::dontSendNotification);
    if (m_avgBPMLabel)
        m_avgBPMLabel->setText(BM_TJ("setPrep.label.avgBPM") + " " +
                               (m_tracks.empty() ? juce::String("-")
                                                 : juce::String(totalBPM / m_tracks.size(), 1)),
                               juce::dontSendNotification);

    if (m_compatibilityLabel)
    {
        if (m_tracks.size() < 2)
            m_compatibilityLabel->setText(BM_TJ("setPrep.label.compatibility") + " -", juce::dontSendNotification);
        else
        {
            int totalScore = 0;
            for (auto& ti : m_transitionInfos) totalScore += ti.scorePercent;
            int avg = !m_transitionInfos.empty() ? totalScore / (int)m_transitionInfos.size() : 0;
            m_compatibilityLabel->setText(BM_TJ("setPrep.label.compatibility") + " " + juce::String(avg) + "%",
                                          juce::dontSendNotification);
        }
    }

    if (m_addBtn)      m_addBtn->setButtonText("+ " + BM_TJ("common.add"));
    if (m_removeBtn)   m_removeBtn->setButtonText(BM_TJ("setPrep.remove"));
    if (m_clearBtn)    m_clearBtn->setButtonText(BM_TJ("setPrep.clear"));
    if (m_exportBtn)   m_exportBtn->setButtonText(BM_TJ("setPrep.export"));
    if (m_moveUpBtn)   m_moveUpBtn->setButtonText(BM_TJ("setPrep.moveUp"));
    if (m_moveDownBtn) m_moveDownBtn->setButtonText(BM_TJ("setPrep.moveDown"));

    if (m_iaAutoArrangeBtn)
    {
        m_iaAutoArrangeBtn->setButtonText(BM_TJ("setPrep.btn.aiArrange"));
        m_iaAutoArrangeBtn->setTooltip(BM_TJ("setPrep.tip.aiArrange"));
    }
    if (m_autoFillBtn)
    {
        m_autoFillBtn->setButtonText(BM_TJ("setPrep.btn.aiFill"));
        m_autoFillBtn->setTooltip(BM_TJ("setPrep.tip.aiFill"));
    }
    if (m_browseLibraryBtn) m_browseLibraryBtn->setButtonText(BM_TJ("setPrep.btn.library"));
    if (m_sortMenuBtn)      m_sortMenuBtn->setButtonText(BM_TJ("setPrep.btn.sort") + juce::String::fromUTF8(" \xe2\x96\xbe"));
    if (m_checkCompatBtn)   m_checkCompatBtn->setButtonText(BM_TJ("setPrep.btn.check"));
    if (m_emptyBrowseBtn)   m_emptyBrowseBtn->setButtonText(BM_TJ("setPrep.btn.library"));
    if (m_emptyAutoFillBtn)
    {
        m_emptyAutoFillBtn->setButtonText(BM_TJ("setPrep.btn.aiFill"));
        m_emptyAutoFillBtn->setTooltip(BM_TJ("setPrep.tip.aiFill"));
    }
    if (m_profileCombo) m_profileCombo->setTextWhenNothingSelected(BM_TJ("setPrep.profile.hint"));
    if (m_setScore)     m_setScore->setTooltip(BM_TJ("setPrep.tip.score"));
    refreshStepsBarTexts();
    updateSteps();

    if (m_transitionEditor)
    {
        rebuild(m_transitionEditor->transitionTypeCombo.get(),
                { "setPrep.settings.transType.harmonic",
                  "setPrep.settings.transType.eqBlend",
                  "setPrep.settings.transType.filterSweep",
                  "setPrep.settings.transType.echoOut",
                  "setPrep.settings.transType.cut",
                  "setPrep.settings.transType.backspin" });
        if (m_transitionEditor->transitionTypeCombo)
            m_transitionEditor->transitionTypeCombo->setTooltip(BM_TJ("setPrep.settings.transTypeTip"));
        if (m_transitionEditor->mixDurationSlider)
            m_transitionEditor->mixDurationSlider->setTooltip(BM_TJ("setPrep.settings.mixDurationTip"));
    }

    if (m_algoSelector)
    {
        if (m_algoSelector->applyBtn)  m_algoSelector->applyBtn->setButtonText(BM_TJ("common.apply"));
        if (m_algoSelector->cancelBtn) m_algoSelector->cancelBtn->setButtonText(BM_TJ("common.cancel"));
    }

    repaint();
}

void SetPreparationView::updateHeaderSubtitle()
{
    if (!m_subtitleLabel) return;
    juce::String sub;
    auto add = [&sub](const juce::String& s) {
        if (s.isEmpty()) return;
        if (sub.isNotEmpty()) sub += juce::String::fromUTF8("  |  ");
        sub += s;
    };
    add(m_eventName);
    add(m_eventDate);
    add(m_djName.isNotEmpty() ? (juce::String::fromUTF8("DJ ") + m_djName) : juce::String());
    add(m_eventVenue);
    if (sub.isEmpty())
        sub = juce::String::fromUTF8("Preparation de set intelligente avec bibliotheque integree");
    m_subtitleLabel->setText(sub, juce::dontSendNotification);
    updateSteps();
}

void SetPreparationView::syncSetInfoFromEditors()
{
    if (m_setNameEditor) m_eventName  = m_setNameEditor->getText();
    if (m_djEditor)      m_djName     = m_djEditor->getText();
    if (m_venueEditor)   m_eventVenue = m_venueEditor->getText();
    if (m_dateEditor)    m_eventDate  = m_dateEditor->getText();
}

void SetPreparationView::refreshStepsBarTexts()
{
    if (!m_stepsBar) return;
    m_stepsBar->stepLabels = { BM_TJ("setPrep.step1"), BM_TJ("setPrep.step2"),
                               BM_TJ("setPrep.step3"), BM_TJ("setPrep.step4"),
                               BM_TJ("setPrep.step5") };
    m_stepsBar->ctaLabels  = { BM_TJ("setPrep.cta.step1"), BM_TJ("setPrep.cta.step2"),
                               BM_TJ("setPrep.cta.step3"), BM_TJ("setPrep.cta.step4"),
                               BM_TJ("setPrep.cta.step5") };
    m_stepsBar->doneLabel  = BM_TJ("setPrep.cta.done");
}

void SetPreparationView::updateSteps()
{
    if (!m_stepsBar) return;
    const bool hasEnoughTracks = m_tracks.size() >= 2;
    m_stepsBar->setStates({
        m_eventName.trim().isNotEmpty(),
        hasEnoughTracks,
        hasEnoughTracks && m_hasArranged,
        hasEnoughTracks && m_validationRun && m_lastValidationOk,
        m_hasExported
    });
    updateProgressiveDisclosure();
}

void SetPreparationView::updateProgressiveDisclosure()
{
    const bool ready = m_tracks.size() >= 2;
    const bool empty = m_tracks.empty();
    const float dimAlpha = ready ? 1.0f : 0.35f;

    auto dim = [&](juce::Component* c) {
        if (!c) return;
        c->setEnabled(ready);
        c->setAlpha(dimAlpha);
    };
    dim(m_profileLabel.get());
    dim(m_profileCombo.get());
    dim(m_setTimeline.get());
    dim(m_setScore.get());
    dim(m_statsDashboard.get());
    dim(m_checklist.get());
    dim(m_exportArchiveBtn.get());
    dim(m_exportDJBtn.get());
    dim(m_exportUsbBtn.get());
    dim(m_exportPrintBtn.get());

    auto gate = [&](juce::Component* c) { if (c) c->setEnabled(ready); };
    gate(m_iaAutoArrangeBtn.get());
    gate(m_sortMenuBtn.get());
    gate(m_checkCompatBtn.get());
    if (m_removeBtn) m_removeBtn->setEnabled(!empty);
    if (m_clearBtn)  m_clearBtn->setEnabled(!empty);

    if (m_setlistTable)    m_setlistTable->setVisible(!empty);
    if (m_emptyBrowseBtn)   m_emptyBrowseBtn->setVisible(empty);
    if (m_emptyAutoFillBtn) m_emptyAutoFillBtn->setVisible(empty);
    repaint();
}

void SetPreparationView::runStepAction(int step)
{
    switch (step)
    {
        case 1:
            if (m_setNameEditor) m_setNameEditor->grabKeyboardFocus();
            break;
        case 2: onBrowseLibrary(); break;
        case 3: onIAAutoArrange(); break;
        case 4: onCheckCompatibility(); break;
        case 5:
            showExportMenu(m_stepsBar ? static_cast<juce::Component*>(m_stepsBar->ctaButton())
                                      : static_cast<juce::Component*>(this));
            break;
        default: break;
    }
}

void SetPreparationView::showExportMenu(juce::Component* anchor)
{
    juce::PopupMenu menu;
    menu.addItem(1, juce::String::fromUTF8("Archiver (.bmset)"));
    menu.addItem(2, juce::String::fromUTF8("Envoyer a un logiciel DJ"));
    menu.addItem(3, juce::String::fromUTF8("Playlist USB (M3U)"));
    menu.addItem(4, juce::String::fromUTF8("Imprimer / Partager"));
    menu.addSeparator();
    menu.addItem(5, BM_TJ("setPrep.export.clone"));

    auto opts = juce::PopupMenu::Options();
    if (anchor) opts = opts.withTargetComponent(anchor);
    menu.showMenuAsync(opts, [this](int result) {
        if (result == 1) onArchiveBmset();
        else if (result == 2) onSendToDJSoftware();
        else if (result == 3) onExportUsbPlaylist();
        else if (result == 4) onPrintShare();
        else if (result == 5) cloneToSoiree();
    });
}

void SetPreparationView::buildChecklistItems()
{
    if (!m_checklist) return;
    m_checklist->items = {
        {juce::String::fromUTF8("Points cue verifies"),     &m_checklistState.cuePointsOK},
        {juce::String::fromUTF8("Transitions marquees"),    &m_checklistState.transitionsOK},
        {juce::String::fromUTF8("Flux d'energie coherent"), &m_checklistState.energyFlowOK},
        {juce::String::fromUTF8("Backup USB prete"),        &m_checklistState.backupReady},
        {juce::String::fromUTF8("Casque + cable RCA"),      &m_checklistState.headphonesOK},
        {juce::String::fromUTF8("Controleur DJ pret"),      &m_checklistState.controllerOK},
        {juce::String::fromUTF8("Gain staging verifie"),    &m_checklistState.gainStagingOK},
        {juce::String::fromUTF8("Setlist imprimee"),        &m_checklistState.setlistPrinted},
    };
}

void SetPreparationView::syncProWidgets()
{
    if (m_energyCurveEditor) m_energyCurveEditor->setTracks(m_tracks);
    if (m_bpmGraph)          m_bpmGraph->setTracks(m_tracks);
    if (m_relatedTracks && !m_tracks.empty())
        m_relatedTracks->setReferenceTrack(m_tracks.back());
}

void SetPreparationView::handleLibraryDrop(const juce::String& trackIdsStr, int insertIndex)
{
    if (!m_provider) return;

    auto idsPart = trackIdsStr.fromFirstOccurrenceOf("BEATMATE_TRACKS:", false, false);
    juce::StringArray idStrings;
    idStrings.addTokens(idsPart, ",", "");

    for (auto& idStr : idStrings)
    {
        int64_t trackId = idStr.getLargeIntValue();
        if (trackId <= 0) continue;

        bool alreadyIn = false;
        for (auto& t : m_tracks)
            if (t.id == trackId) { alreadyIn = true; break; }
        if (alreadyIn) continue;

        auto track = m_provider->getTrack(trackId);
        if (track.id > 0)
        {
            if (insertIndex >= 0 && insertIndex <= (int)m_tracks.size())
                m_tracks.insert(m_tracks.begin() + insertIndex++, track);
            else
                m_tracks.push_back(track);
        }
    }

    m_setlistTable->updateContent();
    updateCompatibility();
    updateTimer();
    updateSetScore();
    refreshStatistics();
}

void SetPreparationView::addTrackToSet(const Models::Track& track)
{
    for (auto& t : m_tracks)
        if (t.id == track.id) return;

    m_tracks.push_back(track);
    m_setlistTable->updateContent();
    updateCompatibility();
    updateTimer();
    updateSetScore();
    refreshStatistics();
}

void SetPreparationView::onIAAutoArrange()
{
    if (m_tracks.size() < 2)
    {
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle("IA Auto-Arrange")
                .withMessage("Ajoutez au moins 2 pistes au set avant d'utiliser l'IA Auto-Arrange.")
                .withButton("OK"),
            nullptr);
        return;
    }
    m_algoSelector->setVisible(true);
    m_algoSelector->toFront(true);
    resized();
}

float SetPreparationView::computeAverageCompatibility(const std::vector<Models::Track>& tracks)
{
    if (tracks.size() < 2) return 0.0f;
    double sum = 0.0;
    int n = 0;
    for (size_t i = 0; i + 1 < tracks.size(); ++i)
    {
        auto r = m_scorer.score(tracks[i], tracks[i + 1]);
        sum += r.score;
        ++n;
    }
    return n > 0 ? static_cast<float>(sum / n) : 0.0f;
}

void SetPreparationView::applyArrangementResult(const std::vector<Models::Track>& before,
                                                const std::vector<Models::Track>& after,
                                                const juce::String& algoName)
{
    int reordered = 0;
    const size_t n = juce::jmin(before.size(), after.size());
    for (size_t i = 0; i < n; ++i)
        if (before[i].id != after[i].id) ++reordered;

    float avgBefore = computeAverageCompatibility(before);
    float avgAfter  = computeAverageCompatibility(after);

    int bestTrackIdx = -1;
    int bestDelta = 0;
    if (before.size() >= 2 && after.size() >= 2)
    {
        for (size_t i = 1; i < after.size(); ++i)
        {
            auto r = m_scorer.score(after[i - 1], after[i]);
            int scoreAfter = r.score;

            int beforePos = -1;
            for (size_t j = 0; j < before.size(); ++j)
                if (before[j].id == after[i].id) { beforePos = (int)j; break; }
            int scoreBefore = 0;
            if (beforePos > 0)
            {
                auto rb = m_scorer.score(before[beforePos - 1], before[beforePos]);
                scoreBefore = rb.score;
            }
            int delta = scoreAfter - scoreBefore;
            if (delta > bestDelta) { bestDelta = delta; bestTrackIdx = (int)i + 1; }
        }
    }

    m_tracks = after;
    m_hasArranged = true;
    m_setlistTable->updateContent();
    updateCompatibility();
    updateSetScore();
    refreshStatistics();
    syncProWidgets();

    juce::String msg;
    msg << algoName << " applique.\n\n"
        << reordered << " positions modifiees.\n"
        << "Compatibilite moyenne: " << juce::String(avgBefore, 1) << "% -> "
        << juce::String(avgAfter, 1) << "%.\n";
    if (bestTrackIdx > 0 && bestDelta > 0)
        msg << "Meilleure amelioration: piste #" << bestTrackIdx
            << " -> +" << bestDelta << " points.";
    else
        msg << "Aucune amelioration notable par piste.";

    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::InfoIcon)
            .withTitle("IA Auto-Arrange - Resultat")
            .withMessage(msg)
            .withButton("OK"),
        nullptr);
}

namespace {
struct AutoArrangeJob : public juce::Thread
{
    AutoArrangeJob() : juce::Thread("AutoArrangeJob") {}
    std::function<void()> work;
    std::function<void()> onDone;
    // WHY: thread self-destructs on the message thread once `onDone` returns,
    void run() override
    {
        if (work) work();
        auto cb = std::move(onDone);
        juce::MessageManager::callAsync([this, cb = std::move(cb)]() {
            if (cb) cb();
            delete this;
        });
    }
};

struct ExportJob : public juce::Thread
{
    ExportJob() : juce::Thread("SetExportJob") {}
    std::function<void()> work;
    std::function<void()> onDone;
    void run() override
    {
        if (work) work();
        auto cb = std::move(onDone);
        juce::MessageManager::callAsync([this, cb = std::move(cb)]() {
            if (cb) cb();
            delete this;
        });
    }
};
}

void SetPreparationView::runExportAsync(const Services::Preparation::ExportConfig& cfg,
                                        const juce::String& destPath,
                                        const juce::String& formatLabel)
{
    if (m_exportBusy->exchange(true))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            juce::String::fromUTF8("Export"),
            juce::String::fromUTF8("Un export est deja en cours. Patientez qu'il se termine."),
            "OK");
        return;
    }

    auto progress = std::make_shared<juce::AlertWindow>(
        juce::String::fromUTF8("Export en cours"),
        juce::String::fromUTF8("Generation et ecriture du fichier en arriere-plan. Veuillez patienter...") ,
        juce::MessageBoxIconType::InfoIcon);
    progress->enterModalState(false, nullptr, false);
    progress->setVisible(true);

    auto tracks   = m_tracks;
    auto config   = cfg;
    auto path     = destPath.toStdString();
    auto resultPtr = std::make_shared<Services::Preparation::ExportResult>();
    auto busy     = m_exportBusy;
    juce::Component::SafePointer<SetPreparationView> selfWeak(this);

    auto* job = new ExportJob();
    job->work = [tracks, config, path, resultPtr]() {
        Services::Preparation::SetExportServiceComplete svc;
        *resultPtr = svc.exportSet(tracks, path, config);
    };
    job->onDone = [progress, resultPtr, busy, formatLabel, path]() {
        progress->exitModalState(0);
        progress->setVisible(false);
        busy->store(false);
        const bool ok = resultPtr->success;
        juce::AlertWindow::showMessageBoxAsync(
            ok ? juce::MessageBoxIconType::InfoIcon
               : juce::MessageBoxIconType::WarningIcon,
            juce::String::fromUTF8("Export ") + formatLabel,
            ok ? (juce::String::fromUTF8("Fichier exporte (")
                  + juce::String(resultPtr->trackCount) + juce::String::fromUTF8(" pistes) :\n")
                  + juce::String(path))
               : (juce::String::fromUTF8("Echec de l'export : ")
                  + juce::String(resultPtr->errorMessage.empty()
                                     ? "erreur inconnue" : resultPtr->errorMessage.c_str())),
            "OK");
        spdlog::info("[SetPrep] export {} -> {} ({})", formatLabel.toStdString(),
                     ok ? "OK" : "FAIL", path);
    };
    job->startThread();
}

void SetPreparationView::runAutoArrange(int algo, float bW, float kW, float eW, float gW, int quickMode)
{
    if (m_tracks.size() < 2) return;

    const auto before = m_tracks;
    const double durationMinutes = m_setDuration ? m_setDuration->getValue() : 120.0;

    auto resultPtr = std::make_shared<std::vector<Models::Track>>();
    auto algoName  = std::make_shared<juce::String>();

    auto work = [resultPtr, algoName, before, durationMinutes, algo, bW, kW, eW, gW, quickMode]() {
        if (quickMode >= 0)
        {
            *algoName = "Quick Mode";
            Services::Preparation::QuickSetPlannerService quickPlanner;
            Services::Preparation::QuickPlanConfig cfg;
            cfg.mode = static_cast<Services::Preparation::QuickPlanMode>(quickMode);
            cfg.durationMinutes = durationMinutes;
            auto r = quickPlanner.planOneClick(before, cfg);
            if (!r.tracks.empty()) *resultPtr = r.tracks;
        }
        else
        {
            Services::Preparation::SetPlannerEngine planner;
            Services::Preparation::PlannerConfig cfg;
            cfg.bpmWeight = bW; cfg.keyWeight = kW; cfg.energyWeight = eW; cfg.genreWeight = gW;
            if (algo == 1)
            {
                *algoName = "Nearest-Neighbor";
                auto r = planner.planNearestNeighbor(before, cfg);
                if (!r.orderedTracks.empty()) *resultPtr = r.orderedTracks;
            }
            else if (algo == 2)
            {
                *algoName = "Optimal exhaustif";
                auto r = planner.planOptimal(before, cfg);
                if (!r.orderedTracks.empty()) *resultPtr = r.orderedTracks;
            }
            else if (algo == 3)
            {
                *algoName = "Simulated Annealing";
                auto r = planner.planSimulatedAnnealing(before, cfg);
                if (!r.orderedTracks.empty()) *resultPtr = r.orderedTracks;
            }
            else if (algo == 4)
            {
                *algoName = "Pure 2-Opt";
                auto r = planner.planOptimal2Opt(before, cfg);
                if (!r.orderedTracks.empty()) *resultPtr = r.orderedTracks;
            }
        }
    };

    const bool useThread = m_tracks.size() > 20;

    if (!useThread)
    {
        work();
        if (!resultPtr->empty())
            applyArrangementResult(before, *resultPtr, *algoName);
        return;
    }

    if (m_autoArrangeBusy->exchange(true))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            juce::String::fromUTF8("IA Auto-Arrange"),
            juce::String::fromUTF8("Une optimisation est deja en cours. Patientez qu'elle se termine."),
            "OK");
        return;
    }

    auto progress = std::make_shared<juce::AlertWindow>(
        "Optimisation en cours",
        "Optimisation du set en arriere-plan. Veuillez patienter...",
        juce::MessageBoxIconType::InfoIcon);
    progress->enterModalState(false, nullptr, false);
    progress->setVisible(true);

    auto busy = m_autoArrangeBusy;
    juce::Component::SafePointer<SetPreparationView> selfWeak(this);

    auto* job = new AutoArrangeJob();
    job->work = work;
    job->onDone = [selfWeak, before, resultPtr, algoName, progress, busy]() {
        progress->exitModalState(0);
        progress->setVisible(false);
        busy->store(false);
        auto* self = selfWeak.getComponent();
        if (self != nullptr && !resultPtr->empty())
            self->applyArrangementResult(before, *resultPtr, *algoName);
    };
    job->startThread();
}

void SetPreparationView::onSortByBPM()
{
    std::sort(m_tracks.begin(), m_tracks.end(),
              [](const Models::Track& a, const Models::Track& b) { return a.bpm < b.bpm; });
    m_hasArranged = true;
    m_setlistTable->updateContent();
    updateCompatibility();
    updateSetScore();
}

void SetPreparationView::onSortByKey()
{
    auto camelotRank = [](const Models::Track& t) {
        juce::String k = juce::String(t.camelotKey).trim().toUpperCase();
        if (k.isEmpty()) k = juce::String(t.key).trim().toUpperCase();
        int num = 0, i = 0;
        while (i < k.length() && k[i] >= '0' && k[i] <= '9')
        {
            num = num * 10 + (int)(k[i] - '0');
            ++i;
        }
        if (num < 1 || num > 12 || i >= k.length()) return 1000;
        const auto letter = k[i];
        if (letter != 'A' && letter != 'B') return 1000;
        return num * 2 + (letter == 'B' ? 1 : 0);
    };
    std::stable_sort(m_tracks.begin(), m_tracks.end(),
              [&camelotRank](const Models::Track& a, const Models::Track& b) {
                  return camelotRank(a) < camelotRank(b);
              });
    m_hasArranged = true;
    m_setlistTable->updateContent();
    updateCompatibility();
    updateSetScore();
}

void SetPreparationView::onSortByEnergy()
{
    std::sort(m_tracks.begin(), m_tracks.end(),
              [](const Models::Track& a, const Models::Track& b) { return a.energy < b.energy; });
    m_hasArranged = true;
    m_setlistTable->updateContent();
    updateCompatibility();
    updateSetScore();
}

void SetPreparationView::onCheckCompatibility()
{
    updateCompatibility();
    updateSetScore();
    refreshValidation();
    refreshStatistics();
    m_setlistTable->repaint();

    auto report = m_validator.validate(m_tracks);
    m_validationRun = true;
    m_lastValidationOk = (report.errorCount == 0);
    if (report.errorCount > 0)
        Widgets::ToastNotifier::getInstance().show(
            juce::String(report.errorCount) + " " + BM_TJ("setPrep.toast.checkErr"), {},
            Widgets::ToastNotifier::Kind::Error, 5000);
    else if (report.warningCount > 0)
        Widgets::ToastNotifier::getInstance().show(
            juce::String(report.warningCount) + " " + BM_TJ("setPrep.toast.checkWarn"), {},
            Widgets::ToastNotifier::Kind::Warning, 5000);
    else
        Widgets::ToastNotifier::getInstance().show(
            BM_TJ("setPrep.toast.checkOk"), {},
            Widgets::ToastNotifier::Kind::Success, 4000);
    updateSteps();
}

void SetPreparationView::onExportM3U()
{
    juce::FileChooser chooser("Exporter en M3U", juce::File{}, "*.m3u");
    if (chooser.browseForFileToSave(true))
    {
        Services::Preparation::ExportConfig cfg;
        cfg.format = Services::Preparation::ExportFormat::M3U;
        cfg.djName = m_djName.toStdString();
        cfg.eventName = m_eventName.toStdString();
        cfg.date = m_eventDate.toStdString();
        runExportAsync(cfg, chooser.getResult().getFullPathName(), "M3U");
    }
}

void SetPreparationView::onExportPDF()
{
    juce::FileChooser chooser("Exporter setlist", juce::File{}, "*.html");
    if (chooser.browseForFileToSave(true))
    {
        Services::Preparation::ExportConfig cfg;
        cfg.format = Services::Preparation::ExportFormat::HTML;
        cfg.includeTransitionNotes = true;
        cfg.djName = m_djName.toStdString();
        cfg.eventName = m_eventName.toStdString();
        cfg.date = m_eventDate.toStdString();
        runExportAsync(cfg, chooser.getResult().getFullPathName(), "HTML");
    }
}

void SetPreparationView::onExportJSON()
{
    juce::FileChooser chooser("Exporter en JSON", juce::File{}, "*.json");
    if (chooser.browseForFileToSave(true))
    {
        Services::Preparation::ExportConfig cfg;
        cfg.format = Services::Preparation::ExportFormat::JSON;
        cfg.djName = m_djName.toStdString();
        cfg.eventName = m_eventName.toStdString();
        cfg.date = m_eventDate.toStdString();
        runExportAsync(cfg, chooser.getResult().getFullPathName(), "JSON");
    }
}

void SetPreparationView::onExportCSV()
{
    juce::FileChooser chooser("Exporter en CSV", juce::File{}, "*.csv");
    if (chooser.browseForFileToSave(true))
    {
        Services::Preparation::ExportConfig cfg;
        cfg.format = Services::Preparation::ExportFormat::CSV;
        cfg.djName = m_djName.toStdString();
        runExportAsync(cfg, chooser.getResult().getFullPathName(), "CSV");
    }
}

void SetPreparationView::onExportHTML()
{
    juce::FileChooser chooser("Exporter en HTML", juce::File{}, "*.html");
    if (chooser.browseForFileToSave(true))
    {
        Services::Preparation::ExportConfig cfg;
        cfg.format = Services::Preparation::ExportFormat::HTML;
        cfg.includeTransitionNotes = true;
        cfg.djName = m_djName.toStdString();
        cfg.eventName = m_eventName.toStdString();
        cfg.date = m_eventDate.toStdString();
        runExportAsync(cfg, chooser.getResult().getFullPathName(), "HTML");
    }
}

void SetPreparationView::onAddTrack()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Selectionner des pistes", juce::File{}, "*.mp3;*.wav;*.flac;*.aac;*.ogg;*.m4a;*.aiff");
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                       | juce::FileBrowserComponent::canSelectFiles
                       | juce::FileBrowserComponent::canSelectMultipleItems,
        [this, chooser](const juce::FileChooser&) {
            auto files = chooser->getResults();
            if (files.isEmpty()) return;
            for (auto& f : files) {
                Models::Track t;
                if (m_provider) {
                    auto dbTracks = m_provider->getAllTracks();
                    bool found = false;
                    auto pathStr = f.getFullPathName().toStdString();
                    for (auto& dbt : dbTracks) {
                        if (dbt.filePath == pathStr) { t = dbt; found = true; break; }
                    }
                    if (!found) {
                        t.title = f.getFileNameWithoutExtension().toStdString();
                        t.filePath = pathStr;
                        t.energy = 5;
                    }
                } else {
                    t.title = f.getFileNameWithoutExtension().toStdString();
                    t.filePath = f.getFullPathName().toStdString();
                    t.energy = 5;
                }
                m_tracks.push_back(t);
            }
            if (m_setlistTable) m_setlistTable->updateContent();
            updateCompatibility();
            updateTimer();
            updateSetScore();
            spdlog::info("[SetPrep] Added {} tracks", files.size());
        });
}

void SetPreparationView::addTracksFromLibrary(const std::vector<int64_t>& trackIds)
{
    if (!m_provider || trackIds.empty()) return;
    int added = 0;
    for (auto id : trackIds) {
        auto t = m_provider->getTrack(id);
        if (t.id > 0) {
            m_tracks.push_back(t);
            added++;
        }
    }
    if (m_setlistTable) m_setlistTable->updateContent();
    updateCompatibility();
    updateTimer();
    updateSetScore();
    spdlog::info("[SetPrep] Added {} tracks from library", added);
}

void SetPreparationView::onRemoveTrack()
{
    auto sel = m_setlistTable->getSelectedRows();
    if (sel.isEmpty())
    {
        Widgets::ToastNotifier::getInstance().show(
            BM_TJ("setPrep.toast.removeHint"), {},
            Widgets::ToastNotifier::Kind::Info, 3500);
        return;
    }
    for (int i = sel.size() - 1; i >= 0; --i)
    {
        int row = sel[i];
        if (row >= 0 && row < (int)m_tracks.size())
            m_tracks.erase(m_tracks.begin() + row);
    }
    m_setlistTable->deselectAllRows();
    m_setlistTable->updateContent();
    updateCompatibility();
    updateTimer();
    updateSetScore();
    refreshStatistics();
}

void SetPreparationView::onClearSet()
{
    if (m_tracks.empty()) return;
    juce::Component::SafePointer<SetPreparationView> self(this);
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon,
        BM_TJ("setPrep.clear"), BM_TJ("setPrep.confirm.clearMsg"),
        BM_TJ("setPrep.clear"), BM_TJ("common.cancel"), this,
        juce::ModalCallbackFunction::create([self](int result) {
            if (result != 1 || self == nullptr) return;
            self->m_tracks.clear();
            self->m_setlistTable->deselectAllRows();
            self->m_setlistTable->updateContent();
            self->updateCompatibility();
            self->updateTimer();
            self->updateSetScore();
            self->refreshStatistics();
            Widgets::ToastNotifier::getInstance().show(
                BM_TJ("setPrep.toast.cleared"), {},
                Widgets::ToastNotifier::Kind::Success, 3000);
        }));
}

void SetPreparationView::onMoveUp()
{
    int sel = m_setlistTable->getSelectedRow();
    if (sel > 0 && sel < (int)m_tracks.size())
    {
        std::swap(m_tracks[sel], m_tracks[sel - 1]);
        m_setlistTable->updateContent();
        m_setlistTable->selectRow(sel - 1);
        updateCompatibility();
        updateSetScore();
    }
}

void SetPreparationView::onMoveDown()
{
    int sel = m_setlistTable->getSelectedRow();
    if (sel >= 0 && sel < (int)m_tracks.size() - 1)
    {
        std::swap(m_tracks[sel], m_tracks[sel + 1]);
        m_setlistTable->updateContent();
        m_setlistTable->selectRow(sel + 1);
        updateCompatibility();
        updateSetScore();
    }
}

void SetPreparationView::onAutoSort()
{
    std::sort(m_tracks.begin(), m_tracks.end(),
              [](const Models::Track& a, const Models::Track& b) { return a.bpm < b.bpm; });
    m_setlistTable->updateContent();
    updateCompatibility();
    updateSetScore();
}

void SetPreparationView::onFillGaps()
{
    if (m_tracks.size() < 2 || !m_provider) return;

    int worstIdx = -1;
    int worstScore = 101;
    for (int i = 0; i < (int)m_transitionInfos.size(); ++i)
    {
        if (m_transitionInfos[i].scorePercent < worstScore)
        {
            worstScore = m_transitionInfos[i].scorePercent;
            worstIdx = i;
        }
    }

    if (worstIdx < 0 || worstScore >= 70) return;

    auto pool = m_provider->getAllTracks();
    auto suggestions = m_scorer.suggestNext(m_tracks[worstIdx], pool, 1);

    if (!suggestions.empty())
    {
        auto filler = m_provider->getTrack(suggestions[0].trackId);
        if (filler.id > 0)
        {
            m_tracks.insert(m_tracks.begin() + worstIdx + 1, filler);
            m_setlistTable->updateContent();
        }
    }

    updateCompatibility();
    updateTimer();
    updateSetScore();
    refreshStatistics();
}

void SetPreparationView::onOptimizeEnergy()
{
    if (m_tracks.size() < 3) return;

    auto sorted = m_tracks;
    std::sort(sorted.begin(), sorted.end(),
              [](const Models::Track& a, const Models::Track& b) { return a.energy < b.energy; });

    const int n = static_cast<int>(sorted.size());

    // Pyramid: even indices [0,2,4,...] ascending form the rising leg, then
    std::vector<Models::Track> pyramid;
    pyramid.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; i += 2) pyramid.push_back(sorted[i]);          // 0,2,4,...
    for (int i = (n % 2 == 0 ? n - 1 : n - 2); i >= 1; i -= 2)
        pyramid.push_back(sorted[i]);                                     // ...,5,3,1
    m_tracks = std::move(pyramid);

    m_setlistTable->updateContent();
    updateCompatibility();
    updateSetScore();
    refreshStatistics();
}

void SetPreparationView::onRandomize()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(m_tracks.begin(), m_tracks.end(), gen);
    m_setlistTable->updateContent();
    updateCompatibility();
    updateSetScore();
}

void SetPreparationView::onExportSetlist()
{
    auto menu = juce::PopupMenu();
    menu.addItem(1, "Export M3U (playlist)");
    menu.addItem(2, "Export HTML (setlist pro)");
    menu.addItem(3, "Export JSON (data)");
    menu.addItem(4, "Export CSV (tableur)");
    menu.addSeparator();
    menu.addItem(5, juce::String::fromUTF8("Cloner en Soirée (.bmsoiree)"));

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(m_exportBtn.get()),
        [this](int result) {
            if (result == 1) onExportM3U();
            else if (result == 2) onExportHTML();
            else if (result == 3) onExportJSON();
            else if (result == 4) onExportCSV();
            else if (result == 5) cloneToSoiree();
        });

    m_listeners.call(&Listener::setlistExportRequested);
}

void SetPreparationView::cloneToSoiree()
{
    if (m_tracks.empty()) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            juce::String::fromUTF8("Cloner en Soirée"),
            juce::String::fromUTF8("Le set est vide — ajoutez d'abord quelques pistes."),
            "OK");
        return;
    }

    double totalSec = 0.0;
    for (const auto& t : m_tracks) totalSec += t.duration;
    const double totalHours = std::max(1.0, std::round((totalSec / 3600.0) * 10.0) / 10.0);
    const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");

    nlohmann::json j;
    j["schema"]   = "bmsoiree/1.0";
    j["name"]     = (juce::String::fromUTF8("Soirée depuis set ") + stamp).toStdString();
    j["venue"]    = "";
    j["date"]     = juce::Time::getCurrentTime().formatted("%Y-%m-%d").toStdString();
    j["durationHours"] = totalHours;
    j["typeIndex"] = 0; // Club default

    // One phase containing all tracks. User can split in Soirée view afterwards.
    nlohmann::json phase;
    phase["name"]          = "Phase principale";
    phase["genre"]         = "";
    phase["targetBPM"]     = 128.0;
    phase["energyTarget"]  = 7;
    phase["durationMin"]   = totalSec / 60.0;
    phase["tracks"]        = nlohmann::json::array();
    for (const auto& t : m_tracks) {
        nlohmann::json jt;
        jt["id"]       = t.id;
        jt["title"]    = t.title;
        jt["artist"]   = t.artist;
        jt["filePath"] = t.filePath;
        jt["bpm"]      = t.bpm;
        jt["key"]      = t.key;
        jt["energy"]   = t.energy;
        jt["duration"] = t.duration;
        phase["tracks"].push_back(std::move(jt));
    }
    j["phases"] = nlohmann::json::array();
    j["phases"].push_back(std::move(phase));

    auto defaultDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("BeatMate").getChildFile("soirees");
    defaultDir.createDirectory();
    auto defaultFile = defaultDir.getChildFile("soiree_" + stamp + ".bmsoiree");

    auto chooser = std::make_shared<juce::FileChooser>(
        juce::String::fromUTF8("Cloner le set en fichier .bmsoiree"),
        defaultFile, "*.bmsoiree");
    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser, j = std::move(j)](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file == juce::File{}) return;
            try {
                std::ofstream out(file.getFullPathName().toStdString());
                out << j.dump(2);
                m_hasExported = true;
                updateSteps();
                spdlog::info("[SetPrep] Cloned to soiree: {}", file.getFullPathName().toStdString());
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                    juce::String::fromUTF8("Soirée créée"),
                    juce::String::fromUTF8("Fichier écrit :\n") + file.getFullPathName() +
                        juce::String::fromUTF8("\n\nOuvrez-le depuis la vue Préparation Soirée (Charger) "
                                                "pour ajuster les phases, la durée et les checklists."),
                    "OK");
            } catch (const std::exception& e) {
                spdlog::error("[SetPrep] Clone to soiree failed: {}", e.what());
            }
        });
}

void SetPreparationView::showAutoFillMenu()
{
    if (!m_provider) { onAutoFillAI({}); return; }

    auto genres = std::make_shared<std::vector<std::pair<juce::String, int>>>();
    auto total  = std::make_shared<int>(0);
    auto* provider = m_provider;
    juce::Component::SafePointer<SetPreparationView> selfWeak(this);

    auto* job = new AutoArrangeJob();
    job->work = [provider, genres, total]() {
        std::map<juce::String, std::pair<juce::String, int>> counts;
        for (auto& t : provider->getAllTracks())
        {
            ++(*total);
            auto g = juce::String(t.genre).trim();
            if (g.isEmpty()) continue;
            auto& entry = counts[g.toLowerCase()];
            if (entry.first.isEmpty()) entry.first = g;
            ++entry.second;
        }
        for (auto& [key, entry] : counts) genres->push_back(entry);
    };
    job->onDone = [selfWeak, genres, total]() {
        auto* self = selfWeak.getComponent();
        if (self == nullptr) return;

        juce::PopupMenu menu;
        menu.addItem(1, BM_TJ("setPrep.fill.allStyles") + " (" + juce::String(*total) + ")", *total > 0);
        if (!genres->empty())
        {
            menu.addSeparator();
            for (int i = 0; i < (int)genres->size(); ++i)
                menu.addItem(100 + i, (*genres)[(size_t)i].first + " (" + juce::String((*genres)[(size_t)i].second) + ")");
        }

        juce::Component* anchor = (self->m_emptyAutoFillBtn && self->m_emptyAutoFillBtn->isVisible())
                                      ? (juce::Component*)self->m_emptyAutoFillBtn.get()
                                      : (juce::Component*)self->m_autoFillBtn.get();
        auto options = anchor ? juce::PopupMenu::Options().withTargetComponent(anchor)
                              : juce::PopupMenu::Options();
        menu.showMenuAsync(options, [selfWeak, genres](int result) {
            auto* s = selfWeak.getComponent();
            if (s == nullptr || result == 0) return;
            if (result == 1) s->onAutoFillAI({});
            else if (result >= 100 && result - 100 < (int)genres->size())
                s->onAutoFillAI((*genres)[(size_t)(result - 100)].first);
        });
    };
    job->startThread();
}

void SetPreparationView::onAutoFillAI(const juce::String& styleFilter)
{
    if (!m_provider)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            juce::String::fromUTF8("Bibliotheque indisponible"),
            juce::String::fromUTF8("Aucune source de pistes n'est connectee - impossible de remplir automatiquement."),
            "OK");
        return;
    }

    if (m_autoFillBusy->exchange(true))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            juce::String::fromUTF8("IA Auto-remplir"),
            juce::String::fromUTF8("Un remplissage automatique est deja en cours. Patientez."),
            "OK");
        return;
    }

    auto progress = std::make_shared<juce::AlertWindow>(
        juce::String::fromUTF8("IA Auto-remplir"),
        juce::String::fromUTF8("Selection des pistes compatibles en arriere-plan. Veuillez patienter..."),
        juce::MessageBoxIconType::InfoIcon);
    progress->enterModalState(false, nullptr, false);
    progress->setVisible(true);

    const double targetSec = (m_setDuration ? m_setDuration->getValue() : 120.0) * 60.0;
    auto resultTracks = std::make_shared<std::vector<Models::Track>>();
    auto addedCount   = std::make_shared<int>(0);
    auto poolEmpty    = std::make_shared<bool>(false);
    auto busy = m_autoFillBusy;
    auto* provider = m_provider;
    const auto seedTracks = m_tracks;
    juce::Component::SafePointer<SetPreparationView> selfWeak(this);

    auto* job = new AutoArrangeJob();
    job->work = [provider, seedTracks, resultTracks, addedCount, poolEmpty, targetSec, styleFilter]() {
        Services::Preparation::SetCompatibilityScorer scorer;

        auto pool = provider->getAllTracks();
        if (styleFilter.isNotEmpty())
        {
            std::vector<Models::Track> filtered;
            for (auto& t : pool)
                if (juce::String(t.genre).trim().equalsIgnoreCase(styleFilter))
                    filtered.push_back(t);
            pool = std::move(filtered);
        }
        if (pool.empty()) { *poolEmpty = true; return; }

        auto trackKey = [](const Models::Track& t) {
            auto title = juce::String(t.title).trim().toLowerCase();
            if (title.isEmpty()) return juce::String(t.id).toStdString();
            return (title + "|" + juce::String(t.artist).trim().toLowerCase()).toStdString();
        };

        std::vector<Models::Track> work = seedTracks;
        std::unordered_set<int64_t> used;
        std::unordered_set<std::string> usedKeys;
        for (auto& t : work) { used.insert(t.id); usedKeys.insert(trackKey(t)); }

        Models::Track seed;
        if (!work.empty()) seed = work.back();
        else { seed = pool.front(); work.push_back(seed); used.insert(seed.id); usedKeys.insert(trackKey(seed)); }

        double cumSec = 0.0;
        for (auto& t : work) cumSec += t.duration;

        int added = 0;
        const int safety = (int)pool.size() + 4;
        for (int guard = 0; guard < safety && cumSec < targetSec; ++guard)
        {
            auto suggestions = scorer.suggestNext(seed, pool, 8);
            Models::Track next;
            bool found = false;
            for (auto& s : suggestions)
            {
                if (used.count(s.trackId)) continue;
                auto cand = provider->getTrack(s.trackId);
                if (cand.id <= 0) continue;
                if (usedKeys.count(trackKey(cand))) { used.insert(cand.id); continue; }
                next = cand; found = true; break;
            }
            if (!found)
            {
                for (auto& cand : pool)
                    if (!used.count(cand.id) && !usedKeys.count(trackKey(cand)))
                    { next = cand; found = true; break; }
            }
            if (!found) break;

            work.push_back(next);
            used.insert(next.id);
            usedKeys.insert(trackKey(next));
            cumSec += (next.duration > 0 ? next.duration : 180.0);
            seed = next;
            ++added;
        }

        *resultTracks = std::move(work);
        *addedCount = added;
    };
    job->onDone = [selfWeak, resultTracks, addedCount, poolEmpty, progress, busy, styleFilter]() {
        progress->exitModalState(0);
        progress->setVisible(false);
        busy->store(false);

        auto* self = selfWeak.getComponent();
        if (self == nullptr) return;

        if (*poolEmpty)
        {
            if (styleFilter.isNotEmpty())
                Widgets::ToastNotifier::getInstance().show(
                    BM_TJ("setPrep.fill.noTracksForStyle") + " (" + styleFilter + ")", {},
                    Widgets::ToastNotifier::Kind::Info, 4000);
            else
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                    juce::String::fromUTF8("Bibliotheque vide"),
                    juce::String::fromUTF8("Aucune piste analysee dans la bibliotheque."),
                    "OK");
            return;
        }

        self->m_tracks = *resultTracks;
        if (self->m_setlistTable) self->m_setlistTable->updateContent();
        if (self->m_setTimeline)  self->m_setTimeline->repaint();
        self->updateCompatibility();
        self->updateTimer();
        self->updateSetScore();
        self->refreshStatistics();

        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            juce::String::fromUTF8("IA Auto-remplir"),
            juce::String(*addedCount) + juce::String::fromUTF8(" morceau(x) ajoute(s) pour atteindre la duree cible."),
            "OK");
    };
    job->startThread();
}

void SetPreparationView::applyEnergyProfile(int profileIndex)
{
    if (profileIndex < 0 || profileIndex >= (int)m_energyPresets.size()) return;
    if (m_tracks.size() < 3)
    {
        Widgets::ToastNotifier::getInstance().show(
            BM_TJ("setPrep.toast.profileNeed3"), {},
            Widgets::ToastNotifier::Kind::Info, 4000);
        return;
    }

    const auto& profile = m_energyPresets[profileIndex];
    const int m = (int)profile.energyLevels.size();
    if (m < 2) return;

    // Cible d'energie pour chaque position du set (interpolation lineaire du preset).
    const int n = (int)m_tracks.size();
    std::vector<float> targets(n);
    for (int i = 0; i < n; ++i)
    {
        float tnorm = (n == 1) ? 0.0f : (float)i / (float)(n - 1);
        float fIdx = tnorm * (m - 1);
        int lo = juce::jlimit(0, m - 1, (int)fIdx);
        int hi = juce::jlimit(0, m - 1, lo + 1);
        float frac = fIdx - lo;
        targets[i] = profile.energyLevels[lo] * (1.0f - frac) + profile.energyLevels[hi] * frac;
    }

    // Affectation gloutonne : pour chaque position, la piste restante dont
    std::vector<Models::Track> remaining = m_tracks;
    std::vector<Models::Track> ordered;
    ordered.reserve(remaining.size());
    for (int i = 0; i < n && !remaining.empty(); ++i)
    {
        int best = 0;
        float bestDiff = std::abs(remaining[0].energy - targets[i]);
        for (int j = 1; j < (int)remaining.size(); ++j)
        {
            float diff = std::abs(remaining[j].energy - targets[i]);
            if (diff < bestDiff) { bestDiff = diff; best = j; }
        }
        ordered.push_back(remaining[best]);
        remaining.erase(remaining.begin() + best);
    }

    m_tracks = std::move(ordered);
    m_hasArranged = true;
    if (m_setlistTable) m_setlistTable->updateContent();
    if (m_setTimeline)  m_setTimeline->repaint();
    updateCompatibility();
    updateSetScore();
    refreshStatistics();
}

void SetPreparationView::runExportToFormat(Services::Preparation::ExportFormat fmt,
                                           const juce::String& wildcard,
                                           const juce::String& formatLabel,
                                           bool withTransitionNotes)
{
    syncSetInfoFromEditors();
    auto chooser = std::make_shared<juce::FileChooser>(
        juce::String::fromUTF8("Exporter - ") + formatLabel, juce::File{}, wildcard);
    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser, fmt, formatLabel, withTransitionNotes](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file == juce::File{}) return;
            m_hasExported = true;
            updateSteps();
            Services::Preparation::ExportConfig cfg;
            cfg.format = fmt;
            cfg.includeTransitionNotes = withTransitionNotes;
            cfg.setName   = m_eventName.toStdString();
            cfg.djName    = m_djName.toStdString();
            cfg.eventName = m_eventName.toStdString();
            cfg.date      = m_eventDate.toStdString();
            runExportAsync(cfg, file.getFullPathName(), formatLabel);
        });
}

void SetPreparationView::onArchiveBmset()
{
    if (m_tracks.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            juce::String::fromUTF8("Archiver"),
            juce::String::fromUTF8("Le set est vide - ajoutez d'abord quelques morceaux."), "OK");
        return;
    }
    syncSetInfoFromEditors();

    auto defaultDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("BeatMate").getChildFile("sets");
    defaultDir.createDirectory();
    const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
    juce::String safeName = m_eventName.isNotEmpty()
        ? m_eventName.replaceCharacters(" /\\", "___") : juce::String("set_") + stamp;
    auto defaultFile = defaultDir.getChildFile(safeName + ".bmset");

    auto chooser = std::make_shared<juce::FileChooser>(
        juce::String::fromUTF8("Archiver le set (.bmset)"), defaultFile, "*.bmset");
    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file == juce::File{}) return;

            nlohmann::json j;
            j["schema"]          = "bmset/1.0";
            j["beatMateVersion"] = BEATMATE_VERSION;
            j["exportedAt"]      = juce::Time::getCurrentTime().toISO8601(true).toStdString();
            j["name"]   = m_eventName.toStdString();
            j["dj"]     = m_djName.toStdString();
            j["venue"]  = m_eventVenue.toStdString();
            j["date"]   = m_eventDate.toStdString();
            j["targetDurationMin"] = m_setDuration ? (int)m_setDuration->getValue() : 120;
            j["setlist"] = nlohmann::json::array();
            for (auto& t : m_tracks)
            {
                nlohmann::json jt;
                jt["id"] = t.id; jt["title"] = t.title; jt["artist"] = t.artist;
                jt["key"] = t.key; jt["camelotKey"] = t.camelotKey; jt["genre"] = t.genre;
                jt["filePath"] = t.filePath; jt["bpm"] = t.bpm; jt["energy"] = t.energy;
                jt["duration"] = t.duration; jt["rating"] = t.rating;
                j["setlist"].push_back(jt);
            }
            j["checklist"] = {
                {"cuePointsOK",    m_checklistState.cuePointsOK},
                {"transitionsOK",  m_checklistState.transitionsOK},
                {"energyFlowOK",   m_checklistState.energyFlowOK},
                {"backupReady",    m_checklistState.backupReady},
                {"headphonesOK",   m_checklistState.headphonesOK},
                {"controllerOK",   m_checklistState.controllerOK},
                {"gainStagingOK",  m_checklistState.gainStagingOK},
                {"setlistPrinted", m_checklistState.setlistPrinted}
            };
            file.replaceWithText(juce::String(j.dump(2)));
            m_hasExported = true;
            updateSteps();
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                juce::String::fromUTF8("Set archive"),
                juce::String::fromUTF8("Fichier ecrit :\n") + file.getFullPathName(), "OK");
            m_listeners.call(&Listener::setlistExportRequested);
        });
}

void SetPreparationView::onExportUsbPlaylist()
{
    if (m_tracks.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            juce::String::fromUTF8("Playlist USB"),
            juce::String::fromUTF8("Le set est vide - ajoutez d'abord quelques morceaux."), "OK");
        return;
    }
    runExportToFormat(Services::Preparation::ExportFormat::M3U, "*.m3u", "Playlist USB (M3U)");
}

void SetPreparationView::onPrintShare()
{
    if (m_tracks.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            juce::String::fromUTF8("Imprimer / Partager"),
            juce::String::fromUTF8("Le set est vide - ajoutez d'abord quelques morceaux."), "OK");
        return;
    }
    runExportToFormat(Services::Preparation::ExportFormat::HTML, "*.html", "Imprimer / Partager (HTML)", true);
}

void SetPreparationView::onSendToDJSoftware()
{
    if (m_tracks.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            juce::String::fromUTF8("Envoyer a un logiciel DJ"),
            juce::String::fromUTF8("Le set est vide - ajoutez d'abord quelques morceaux."), "OK");
        return;
    }

    extern BeatMate::ServiceLocator* g_serviceLocator;
    auto* router = g_serviceLocator
        ? g_serviceLocator->tryGet<Services::DJSoftware::SendToDJRouter>() : nullptr;
    if (!router)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            juce::String::fromUTF8("Envoyer a un logiciel DJ"),
            juce::String::fromUTF8("Le routeur d'envoi DJ n'est pas disponible."), "OK");
        return;
    }

    using DJT = Services::DJSoftware::DJTarget;
    auto status = router->pingTargets();
    auto labelFor = [&status](DJT t) {
        juce::String lbl = juce::String(Services::DJSoftware::SendToDJRouter::targetLabel(t));
        auto it = status.find(t);
        if (it != status.end()) lbl += juce::String::fromUTF8("  (") + juce::String(it->second) + ")";
        return lbl;
    };

    juce::PopupMenu menu;
    menu.addSectionHeader(juce::String::fromUTF8("Envoyer ") + juce::String((int)m_tracks.size())
                          + juce::String::fromUTF8(" morceaux vers"));
    menu.addItem(1, juce::String::fromUTF8("Auto (logiciel ouvert)"));
    menu.addSeparator();
    menu.addItem(2, labelFor(DJT::Rekordbox), router->isInstalled(DJT::Rekordbox), false);
    menu.addItem(3, labelFor(DJT::Serato),    router->isInstalled(DJT::Serato),    false);
    menu.addItem(4, labelFor(DJT::Traktor),   router->isInstalled(DJT::Traktor),   false);
    menu.addItem(5, labelFor(DJT::VirtualDJ), router->isInstalled(DJT::VirtualDJ), false);
    menu.addItem(6, labelFor(DJT::EngineDJ),  router->isInstalled(DJT::EngineDJ),  false);

    auto tracksCopy = m_tracks;
    juce::Component::SafePointer<SetPreparationView> selfWeak(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(m_exportDJBtn.get()),
        [router, tracksCopy, selfWeak](int result) {
            if (result == 0) return;
            if (auto* self = selfWeak.getComponent())
            {
                self->m_hasExported = true;
                self->updateSteps();
            }
            DJT target = DJT::Auto;
            switch (result)
            {
                case 1: target = DJT::Auto;      break;
                case 2: target = DJT::Rekordbox; break;
                case 3: target = DJT::Serato;    break;
                case 4: target = DJT::Traktor;   break;
                case 5: target = DJT::VirtualDJ; break;
                case 6: target = DJT::EngineDJ;  break;
                default: return;
            }
            auto res = router->sendTracks(tracksCopy, target);
            const juce::String label = juce::String(
                Services::DJSoftware::SendToDJRouter::targetLabel(res.target));
            juce::AlertWindow::showMessageBoxAsync(
                res.ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                juce::String::fromUTF8("Envoi vers ") + label,
                juce::String(res.message.empty()
                    ? (res.ok ? "Set envoye." : "Echec de l'envoi.")
                    : res.message.c_str()),
                "OK");
        });
}

static juce::File getSetChecklistFile()
{
    auto appDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                      .getChildFile("BeatMate");
    if (!appDir.isDirectory()) appDir.createDirectory();
    return appDir.getChildFile("set_checklist.json");
}

void SetPreparationView::saveChecklistState()
{
    nlohmann::json j;
    j["cuePointsOK"]    = m_checklistState.cuePointsOK;
    j["transitionsOK"]  = m_checklistState.transitionsOK;
    j["energyFlowOK"]   = m_checklistState.energyFlowOK;
    j["backupReady"]    = m_checklistState.backupReady;
    j["headphonesOK"]   = m_checklistState.headphonesOK;
    j["controllerOK"]   = m_checklistState.controllerOK;
    j["gainStagingOK"]  = m_checklistState.gainStagingOK;
    j["setlistPrinted"] = m_checklistState.setlistPrinted;
    const std::string dumped = j.dump(2);

    bool dbOk = false;
    if (BeatMate::g_serviceLocator)
        if (auto* store = BeatMate::g_serviceLocator->tryGet<Services::Persistence::SettingsStore>())
            dbOk = store->upsertSetlist("set_checklist", dumped);
    getSetChecklistFile().replaceWithText(juce::String(dumped));
    if (!dbOk)
        spdlog::warn("[SetPrep] saveChecklistState: DB write failed (JSON fallback only)");
}

void SetPreparationView::loadChecklistState()
{
    std::string payload;
    if (BeatMate::g_serviceLocator)
        if (auto* store = BeatMate::g_serviceLocator->tryGet<Services::Persistence::SettingsStore>())
            if (auto blob = store->getSetlist("set_checklist"); blob.has_value())
                payload = blob->jsonPayload;
    if (payload.empty())
    {
        auto file = getSetChecklistFile();
        if (!file.existsAsFile()) return;
        payload = file.loadFileAsString().toStdString();
    }
    try
    {
        auto j = nlohmann::json::parse(payload);
        m_checklistState.cuePointsOK    = j.value("cuePointsOK", false);
        m_checklistState.transitionsOK  = j.value("transitionsOK", false);
        m_checklistState.energyFlowOK   = j.value("energyFlowOK", false);
        m_checklistState.backupReady    = j.value("backupReady", false);
        m_checklistState.headphonesOK   = j.value("headphonesOK", false);
        m_checklistState.controllerOK   = j.value("controllerOK", false);
        m_checklistState.gainStagingOK  = j.value("gainStagingOK", false);
        m_checklistState.setlistPrinted = j.value("setlistPrinted", false);
    }
    catch (...) {}
}

void SetPreparationView::updateCompatibility()
{
    if (!m_trackCountLabel || !m_setlistTable || !m_energyCurve) return;
    m_trackCountLabel->setText(BM_TJ("setPrep.label.tracks") + " " + juce::String((int)m_tracks.size()), juce::dontSendNotification);

    m_transitionInfos.clear();

    for (size_t i = 0; i + 1 < m_tracks.size(); ++i)
    {
        auto result = m_scorer.score(m_tracks[i], m_tracks[i + 1]);

        float bpmDiff = static_cast<float>(m_tracks[i + 1].bpm - m_tracks[i].bpm);

        juce::Colour col;
        if (result.score >= 75)
            col = Colors::success();
        else if (result.score >= 45)
            col = Colors::warning();
        else
            col = Colors::error();

        TransitionInfo ti;
        ti.scorePercent = result.score;
        ti.bpmDiff = bpmDiff;
        // Threshold aligned with scorer "Compatible" tier (>=18 covers +2 semi mood shift,
        ti.keyCompat = result.keyScore >= 15;
        ti.transitionType = ti.keyCompat ? juce::String(result.advice)
                                         : BM_TJ("setPrep.advice.keyClash");
        ti.color = col;
        ti.fullResult = result;
        m_transitionInfos.push_back(ti);
    }

    m_energyCurve->energyValues.clear();
    m_energyCurve->trackNames.clear();
    m_energyCurve->intraCurves.clear();
    for (auto& t : m_tracks)
    {
        m_energyCurve->energyValues.push_back(static_cast<int>(t.energy));
        m_energyCurve->trackNames.push_back(juce::String(t.title));

        std::vector<int> intra;
        if (!t.energySegments.empty())
        {
            try
            {
                auto segs = nlohmann::json::parse(t.energySegments);
                for (const auto& s : segs)
                    intra.push_back(std::clamp(s.value("energy", 0), 1, 10));
            }
            catch (const nlohmann::json::exception&) { intra.clear(); }
        }
        if (intra.size() > 7)
        {
            std::vector<int> reduced;
            for (int j = 0; j < 7; ++j)
                reduced.push_back(intra[(size_t)std::llround(
                    j * (intra.size() - 1) / 6.0)]);
            intra = std::move(reduced);
        }
        m_energyCurve->intraCurves.push_back(std::move(intra));
    }
    m_energyCurve->repaint();

    m_setlistTable->updateContent();
    m_setlistTable->repaint();
    if (m_setTimeline) m_setTimeline->repaint();

    m_persistDirty = true;
    startTimer(500);
    refreshValidation();
    m_validationRun = false;

    if (m_transitionEditor && m_transitionEditor->isVisible())
    {
        const int idx = m_transitionEditor->transitionIndex;
        if (idx >= 0 && idx + 1 < (int)m_tracks.size())
        {
            auto result = m_matchUp.matchTracks(m_tracks[(size_t)idx], m_tracks[(size_t)idx + 1]);
            const int rowScore = idx < (int)m_transitionInfos.size()
                ? m_transitionInfos[(size_t)idx].scorePercent : -1;
            m_transitionEditor->updateFromResult(result, idx, rowScore);
        }
        else
            m_transitionEditor->setVisible(false);
    }

    updateSteps();
}

void SetPreparationView::timerCallback()
{
    stopTimer();
    if (m_persistDirty)
    {
        m_persistDirty = false;
        saveSetToJSON();
    }
}

void SetPreparationView::updateTimer()
{
    if (!m_totalDurationLabel || !m_timerLabel || !m_avgBPMLabel) return;
    double totalSec = 0;
    float totalBPM = 0;
    for (auto& t : m_tracks)
    {
        totalSec += t.duration;
        totalBPM += static_cast<float>(t.bpm);
    }

    int hours = (int)(totalSec / 3600);
    int mins = (int)(totalSec / 60) % 60;
    int secs = (int)totalSec % 60;
    m_totalDurationLabel->setText(BM_TJ("setPrep.label.duration") + " " + juce::String::formatted("%d:%02d:%02d", hours, mins, secs),
                                 juce::dontSendNotification);
    m_timerLabel->setText(BM_TJ("setPrep.label.timer") + " " + juce::String(hours) + "h " + juce::String::formatted("%02d", mins) + "m",
                         juce::dontSendNotification);

    if (!m_tracks.empty())
        m_avgBPMLabel->setText(BM_TJ("setPrep.label.avgBPM") + " " + juce::String(totalBPM / m_tracks.size(), 1), juce::dontSendNotification);
    else
        m_avgBPMLabel->setText(BM_TJ("setPrep.label.avgBPM") + " -", juce::dontSendNotification);

    if (m_setTimeline)
    {
        m_setTimeline->targetDurationMin = m_setDuration ? m_setDuration->getValue() : 120.0;
        m_setTimeline->repaint();
    }
}

void SetPreparationView::updateSetScore()
{
    if (!m_setScore || !m_compatibilityLabel) return;

    syncProWidgets();

    if (m_tracks.size() < 2)
    {
        m_setScore->score = 0;
        m_setScore->repaint();
        m_compatibilityLabel->setText(BM_TJ("setPrep.label.compatibility") + " -", juce::dontSendNotification);
        return;
    }

    int totalScore = 0;
    for (auto& ti : m_transitionInfos)
        totalScore += ti.scorePercent;

    int avg = !m_transitionInfos.empty() ? totalScore / (int)m_transitionInfos.size() : 0;
    m_setScore->score = juce::jlimit(0, 100, avg);
    m_setScore->repaint();

    juce::Colour scoreColor = avg >= 75 ? Colors::success() : avg >= 45 ? Colors::warning() : Colors::error();
    m_compatibilityLabel->setColour(juce::Label::textColourId, scoreColor);
    m_compatibilityLabel->setText(BM_TJ("setPrep.label.compatibility") + " " + juce::String(avg) + "%", juce::dontSendNotification);
}

void SetPreparationView::refreshStatistics()
{
    if (!m_statsDashboard) return;
    if (m_tracks.empty())
    {
        m_statsDashboard->stats = {};
        m_statsDashboard->repaint();
        return;
    }
    auto stats = m_statsService.computeStatistics(m_tracks);
    m_statsDashboard->stats = stats;
    m_statsDashboard->repaint();
}

void SetPreparationView::refreshValidation()
{
    if (!m_validationPanel) return;
    m_validationPanel->trackCount = (int)m_tracks.size();
    auto report = m_validator.validate(m_tracks);
    m_validationPanel->setReport(report);
}

namespace {
class SetAddTracksDialogContent : public juce::Component
{
public:
    SetAddTracksDialogContent(LibraryBrowserPanel* browser,
                              std::function<void(const std::vector<Models::Track>&)> onAdd)
        : m_browser(browser), m_onAdd(std::move(onAdd))
    {
        if (m_browser) addAndMakeVisible(*m_browser);

        m_hint = std::make_unique<juce::Label>("hint",
            juce::String::fromUTF8("Selectionne une ou plusieurs pistes puis clique 'Ajouter au set' "
                                    "(double-clic fonctionne aussi)."));
        m_hint->setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.75f));
        m_hint->setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
        addAndMakeVisible(*m_hint);

        m_addBtn = std::make_unique<juce::TextButton>(juce::String::fromUTF8("Ajouter au set"));
        m_addBtn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF22C55E));
        m_addBtn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        m_addBtn->onClick = [this] {
            if (!m_browser) return;
            auto sel = m_browser->getSelectedTracks();
            if (sel.empty()) {
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::InfoIcon)
                        .withTitle(juce::String::fromUTF8("Aucune piste selectionnee"))
                        .withMessage(juce::String::fromUTF8("Selectionne au moins une piste avant d'ajouter."))
                        .withButton("OK"),
                    nullptr);
                return;
            }
            if (m_onAdd) m_onAdd(sel);
            if (auto* top = getTopLevelComponent())
                if (auto* dw = dynamic_cast<juce::DialogWindow*>(top))
                    dw->exitModalState(0);
        };
        addAndMakeVisible(*m_addBtn);

        m_closeBtn = std::make_unique<juce::TextButton>(juce::String::fromUTF8("Fermer"));
        m_closeBtn->onClick = [this] {
            if (auto* top = getTopLevelComponent())
                if (auto* dw = dynamic_cast<juce::DialogWindow*>(top))
                    dw->exitModalState(0);
        };
        addAndMakeVisible(*m_closeBtn);

        setSize(780, 600);
    }

    ~SetAddTracksDialogContent() override {
        if (m_browser) removeChildComponent(m_browser);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(10);
        m_hint->setBounds(bounds.removeFromTop(22));
        bounds.removeFromTop(4);
        auto bottom = bounds.removeFromBottom(36);
        m_closeBtn->setBounds(bottom.removeFromRight(100));
        bottom.removeFromRight(8);
        m_addBtn->setBounds(bottom.removeFromRight(180));
        bounds.removeFromBottom(6);
        if (m_browser) m_browser->setBounds(bounds);
    }

private:
    LibraryBrowserPanel* m_browser = nullptr;
    std::unique_ptr<juce::Label>      m_hint;
    std::unique_ptr<juce::TextButton> m_addBtn;
    std::unique_ptr<juce::TextButton> m_closeBtn;
    std::function<void(const std::vector<Models::Track>&)> m_onAdd;
};
} // anonymous namespace

void SetPreparationView::onBrowseLibrary()
{
    if (!m_libraryBrowser) return;

    if (m_libraryBrowser->isShowing())
    {
        if (auto* top = m_libraryBrowser->getTopLevelComponent())
            top->toFront(true);
        return;
    }

    m_libraryBrowser->refreshResults();
    m_libraryBrowser->setVisible(true);

    auto* content = new SetAddTracksDialogContent(
        m_libraryBrowser.get(),
        [this](const std::vector<Models::Track>& tracks) {
            for (auto& t : tracks) addTrackToSet(t);
        });

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(content);
    opts.dialogTitle                  = juce::String::fromUTF8("Ajouter des pistes au set");
    opts.dialogBackgroundColour       = juce::Colour(0xFF0E1117);
    opts.componentToCentreAround      = this;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar            = true;
    opts.resizable                    = true;
    opts.useBottomRightCornerResizer  = false;
    opts.launchAsync();
}

void SetPreparationView::paint(juce::Graphics& g)
{
    const int W = getWidth();
    const int H = getHeight();
    const int margin = 16;
    const int headerH = 56;

    ProDraw::viewBackground(g, W, H);

    {
        g.setColour(Colors::bgDarkest().withAlpha(0.9f));
        g.fillRect(0.0f, 0.0f, (float)W, (float)headerH);

        g.setFont(juce::Font("Segoe UI", 22.0f, juce::Font::bold));
        g.setColour(Colors::textPrimary());
        g.drawText(juce::String::fromUTF8("PREPARATION DU SET"), margin, 6, 360, 28, juce::Justification::centredLeft);

        juce::ColourGradient sepGrad(
            Colors::accent().withAlpha(0.0f), (float)margin, (float)headerH - 1.0f,
            Colors::accent().withAlpha(0.8f), (float)(W / 2), (float)headerH - 1.0f, false);
        g.setGradientFill(sepGrad);
        g.fillRect((float)margin, (float)(headerH - 2), (float)(W / 2 - margin), 2.0f);
        juce::ColourGradient sepGrad2(
            Colors::accent().withAlpha(0.8f), (float)(W / 2), (float)headerH - 1.0f,
            Colors::accent().withAlpha(0.0f), (float)(W - margin), (float)headerH - 1.0f, false);
        g.setGradientFill(sepGrad2);
        g.fillRect((float)(W / 2), (float)(headerH - 2), (float)(W / 2 - margin), 2.0f);
    }

    const int gap = 8;
    const int contentTop = headerH + kStepsBarH + 6;
    const int contentH = H - contentTop - 12;
    const int leftW  = juce::jmax(240, W / 4);
    const int rightW = juce::jmax(240, W / 4);
    const int centerX = margin + leftW + gap;
    const int centerW = W - leftW - rightW - margin * 2 - gap * 2;
    const int rightX  = W - rightW - margin;

    auto drawGlassPanel = [&](juce::Rectangle<int> r)
    {
        ProDraw::glassPanel(g, r.toFloat(), 12.0f);
    };
    auto drawSectionLabel = [&](const juce::String& text, int x, int y, juce::Colour accentCol)
    {
        ProDraw::sectionHeader(g, text, x, y, accentCol);
    };

    const bool lockedSides = m_tracks.size() < 2;

    drawGlassPanel({margin, contentTop, leftW, contentH});
    drawSectionLabel(juce::String::fromUTF8("INFOS SET"), margin + 10, contentTop + 8, Colors::primary());
    drawSectionLabel(juce::String::fromUTF8("PROFIL D'ENERGIE"), margin + 10, contentTop + 168, Colors::success());

    drawGlassPanel({centerX, contentTop, centerW, contentH});
    drawSectionLabel(juce::String::fromUTF8("MORCEAUX DU SET"), centerX + 10, contentTop + 8, Colors::accent());

    drawGlassPanel({rightX, contentTop, rightW, contentH});
    drawSectionLabel(juce::String::fromUTF8("CHECKLIST & EXPORT"), rightX + 10, contentTop + 8, Colors::secondary());

    if (lockedSides)
    {
        g.setFont(juce::Font("Segoe UI", 10.0f, juce::Font::italic));
        g.setColour(Colors::textMuted());
        g.drawFittedText(BM_TJ("setPrep.locked.hint"),
                         margin + 10, contentTop + 154, leftW - 20, 12,
                         juce::Justification::centredLeft, 1);
        g.drawFittedText(BM_TJ("setPrep.locked.hint"),
                         rightX + 10, contentTop + contentH - 24, rightW - 20, 12,
                         juce::Justification::centredLeft, 1);
    }

    const int cInnerX = centerX + 14;
    const int headerY = contentTop + 28 + 30 + 36 + 8; // section + stats row + bouton bar
    const int colHeaderH = 18;
    ProDraw::glassPanel(g, juce::Rectangle<float>((float)(centerX + 6), (float)(headerY - 2),
                         (float)(centerW - 12), (float)(colHeaderH + 2)), 4.0f);

    g.setFont(juce::Font("Segoe UI", 10.0f, juce::Font::bold));
    auto drawColHeader = [&](const juce::String& text, int x, int y, int w, SortColumn col, juce::Justification just)
    {
        if (m_sortColumn == col)
        {
            g.setColour(Colors::primary().withAlpha(0.25f));
            g.fillRoundedRectangle((float)x - 3, (float)y - 2, (float)w + 6, (float)colHeaderH + 2, 3.0f);
            g.setColour(Colors::primary());
        }
        else
            g.setColour(Colors::textSecondary());
        g.drawText(text, x, y, w, 14, just);
        if (m_sortColumn == col)
        {
            juce::String arrow = m_sortAscending ? juce::CharPointer_UTF8("\xe2\x96\xb2") : juce::CharPointer_UTF8("\xe2\x96\xbc");
            g.setFont(juce::Font("Segoe UI", 9.0f, juce::Font::bold));
            g.drawText(arrow, x + w - 12, y, 12, 14, juce::Justification::centredRight);
            g.setFont(juce::Font("Segoe UI", 10.0f, juce::Font::bold));
        }
    };

    int hx = cInnerX + 18;
    drawColHeader("#",       hx, headerY, 22,  SortColumn::Number,   juce::Justification::centredLeft); hx += 26;
    drawColHeader("TITRE",   hx, headerY, 180, SortColumn::Title,    juce::Justification::centredLeft); hx += 184;
    drawColHeader("ARTISTE", hx, headerY, 120, SortColumn::Artist,   juce::Justification::centredLeft); hx += 124;
    drawColHeader("BPM",     hx, headerY, 52,  SortColumn::BPM,      juce::Justification::centred);    hx += 56;
    drawColHeader("KEY",     hx, headerY, 36,  SortColumn::Key,      juce::Justification::centred);    hx += 40;
    drawColHeader("ENERGY",  hx, headerY, 54,  SortColumn::Energy,   juce::Justification::centred);    hx += 78;
    drawColHeader("DUREE",   hx, headerY, 44,  SortColumn::Duration, juce::Justification::centred);

    if (m_tracks.empty())
    {
        const int listTop = contentTop + 62 + 36 + 18 + 4;
        const int listH = juce::jmax(160, contentTop + contentH - listTop - 46 - 16);
        auto emptyArea = juce::Rectangle<int>(centerX + 24, listTop, centerW - 48, listH)
                             .withTrimmedBottom(96);
        ProDraw::emptyState(g, emptyArea, "+",
                            BM_TJ("setPrep.empty.title"),
                            BM_TJ("setPrep.empty.detail"),
                            Colors::modulePrep());
    }

    ProDraw::vignette(g, (float)W, (float)H, 10.0f);
}

void SetPreparationView::resized()
{
    // Guard against being laid out before setupUI has finished.
    if (!m_titleLabel || !m_setlistTable || !m_setNameEditor || !m_checklist) return;

    const int W = getWidth();
    const int H = getHeight();
    const int margin = 16;
    const int headerH = 56;
    const int gap = 8;
    const int panelPad = 14;
    const int lw = 64; // largeur des libelles

    m_titleLabel->setVisible(false);
    m_subtitleLabel->setBounds(margin + 360, 18, juce::jmax(120, W - margin - 360 - 12), 22);
    m_subtitleLabel->setColour(juce::Label::textColourId, Colors::textMuted());
    m_subtitleLabel->setFont(juce::Font("Segoe UI", 12.0f, juce::Font::plain));

    if (m_stepsBar)
        m_stepsBar->setBounds(margin, headerH + 2, W - margin * 2, kStepsBarH - 4);

    const int contentTop = headerH + kStepsBarH + 6;
    const int contentH = H - contentTop - 12;
    const int leftW  = juce::jmax(240, W / 4);
    const int rightW = juce::jmax(240, W / 4);
    const int centerX = margin + leftW + gap;
    const int centerW = W - leftW - rightW - margin * 2 - gap * 2;
    const int rightX  = W - rightW - margin;

    int lx = margin + panelPad;
    int lInnerW = leftW - panelPad * 2;
    int ly = contentTop + 30;

    auto infoRow = [&](juce::Label* lbl, juce::Component* field) {
        if (lbl)   lbl->setBounds(lx, ly, lw, 22);
        if (field) field->setBounds(lx + lw, ly, lInnerW - lw, 24);
        ly += 28;
    };
    infoRow(m_setNameLabel.get(), m_setNameEditor.get());
    infoRow(m_djLabel.get(),      m_djEditor.get());
    infoRow(m_venueLabel.get(),   m_venueEditor.get());
    infoRow(m_dateLabel.get(),    m_dateEditor.get());
    if (m_durationLabel) m_durationLabel->setBounds(lx, ly, lw, 22);
    if (m_setDuration)   m_setDuration->setBounds(lx + lw, ly, lInnerW - lw, 22);

    ly = contentTop + 186;
    if (m_profileCombo) m_profileCombo->setBounds(lx, ly, lInnerW, 26);

    ly = contentTop + 222;
    int timelineH = juce::jmax(90, contentTop + contentH - ly - panelPad);
    if (m_setTimeline) m_setTimeline->setBounds(lx, ly, lInnerW, timelineH);

    int cx = centerX + panelPad;
    int cInnerW = centerW - panelPad * 2;

    int sy = contentTop + 30;
    int sx = cx;
    m_trackCountLabel->setBounds(sx, sy, 78, 24); sx += 80;
    m_totalDurationLabel->setBounds(sx, sy, 110, 24); sx += 112;
    m_avgBPMLabel->setBounds(sx, sy, 96, 24); sx += 98;
    m_compatibilityLabel->setBounds(sx, sy, 118, 24); sx += 120;
    m_timerLabel->setBounds(sx, sy, 96, 24);

    int btnBarY = contentTop + 30 + 32;
    int btnH = 28;
    int bx = cx;
    m_browseLibraryBtn->setBounds(bx, btnBarY, 118, btnH); bx += 122;
    m_autoFillBtn->setBounds(bx, btnBarY, 104, btnH); bx += 116;
    m_iaAutoArrangeBtn->setBounds(bx, btnBarY, 104, btnH); bx += 108;
    m_sortMenuBtn->setBounds(bx, btnBarY, 72, btnH); bx += 84;
    m_removeBtn->setBounds(bx, btnBarY, 70, btnH); bx += 74;
    m_clearBtn->setBounds(bx, btnBarY, 62, btnH); bx += 74;
    m_checkCompatBtn->setBounds(bx, btnBarY, 78, btnH);

    if (m_exportBtn) m_exportBtn->setVisible(false);
    if (m_addBtn)    m_addBtn->setVisible(false);

    int colHeaderH = 18;
    int listTop = btnBarY + 36 + colHeaderH + 4;
    int transitionEditorH = m_transitionEditor->isVisible() ? 118 : 0;
    int validationH = 46;
    const int validationTop = contentTop + contentH - validationH - panelPad;
    const int editorTop = validationTop - (transitionEditorH > 0 ? transitionEditorH + 4 : 0);
    int listH = editorTop - 4 - listTop;
    m_setlistTable->setBounds(cx, listTop, cInnerW, juce::jmax(listH, 160));

    if (m_emptyBrowseBtn && m_emptyAutoFillBtn)
    {
        const int listCentreY = listTop + juce::jmax(listH, 160) / 2;
        const int ebW = 170, eaW = 150, ebH = 32, gap2 = 12;
        const int totalW = ebW + gap2 + eaW;
        const int ex = cx + (cInnerW - totalW) / 2;
        const int ey = listCentreY + 58;
        m_emptyBrowseBtn->setBounds(ex, ey, ebW, ebH);
        m_emptyAutoFillBtn->setBounds(ex + ebW + gap2, ey, eaW, ebH);
    }

    if (m_transitionEditor->isVisible())
        m_transitionEditor->setBounds(cx, editorTop, cInnerW, transitionEditorH);

    m_validationPanel->setBounds(cx, validationTop, cInnerW, validationH);

    int rx = rightX + panelPad;
    int rInnerW = rightW - panelPad * 2;
    int ry = contentTop + 30;

    m_setScore->setBounds(rx, ry, rInnerW, 96);
    ry += 104;

    m_statsDashboard->setBounds(rx, ry, rInnerW, 120);
    ry += 128;

    int checklistH = 28 + 8 * 30 + 8;
    m_checklist->setBounds(rx, ry, rInnerW, checklistH);
    ry += checklistH + 12;

    int exH = 32;
    m_exportArchiveBtn->setBounds(rx, ry, rInnerW, exH); ry += exH + 8;
    m_exportDJBtn->setBounds(rx, ry, rInnerW, exH);      ry += exH + 8;
    m_exportUsbBtn->setBounds(rx, ry, rInnerW, exH);     ry += exH + 8;
    m_exportPrintBtn->setBounds(rx, ry, rInnerW, exH);

    // Widgets pro lourds : conserves/synchronises mais masques pour garder
    if (m_energyCurve)       { m_energyCurve->setBounds(rx, ry, rInnerW, 1); m_energyCurve->setVisible(false); }
    if (m_energyCurveEditor) m_energyCurveEditor->setVisible(false);
    if (m_bpmGraph)          m_bpmGraph->setVisible(false);
    if (m_relatedTracks)     m_relatedTracks->setVisible(false);

    if (m_algoSelector && m_algoSelector->isVisible())
    {
        int aW = 380, aH = 340;
        m_algoSelector->setBounds((W - aW) / 2, (H - aH) / 2, aW, aH);
    }
}

static juce::File getSetPersistenceFile()
{
    auto appDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                      .getChildFile("BeatMate");
    if (!appDir.isDirectory()) appDir.createDirectory();
    return appDir.getChildFile("current_set.json");
}

void SetPreparationView::saveSetToJSON()
{
    try
    {
        nlohmann::json j;
        j["setlist"] = nlohmann::json::array();
        for (auto& t : m_tracks)
        {
            nlohmann::json jt;
            jt["id"] = t.id;
            jt["title"] = t.title;
            jt["artist"] = t.artist;
            jt["key"] = t.key;
            jt["camelotKey"] = t.camelotKey;
            jt["filePath"] = t.filePath;
            jt["bpm"] = t.bpm;
            jt["energy"] = t.energy;
            jt["duration"] = t.duration;
            jt["genre"] = t.genre;
            jt["rating"] = t.rating;
            j["setlist"].push_back(jt);
        }
        j["djName"] = m_djName.toStdString();
        j["eventName"] = m_eventName.toStdString();
        j["eventDate"] = m_eventDate.toStdString();
        j["eventVenue"] = m_eventVenue.toStdString();

        const std::string dumped = j.dump(2);

        // Primary persistence: DB via SettingsStore (keyed by "current").
        bool dbOk = false;
        if (BeatMate::g_serviceLocator) {
            if (auto* store = BeatMate::g_serviceLocator->tryGet<Services::Persistence::SettingsStore>()) {
                dbOk = store->upsertSetlist("current", dumped);
            }
        }
        // Secondary: legacy JSON file remains as export/import format.
        auto file = getSetPersistenceFile();
        std::ofstream out(file.getFullPathName().toStdString());
        if (out.is_open()) out << dumped;
        if (!dbOk)
            spdlog::warn("[SetPreparationView] saveSetToJSON: DB write failed, JSON fallback used");
    }
    catch (...) {}
}

void SetPreparationView::loadSetFromJSON()
{
    // Prefer DB when SettingsStore is up. Fall back to legacy JSON.
    std::string payload;
    if (BeatMate::g_serviceLocator) {
        if (auto* store = BeatMate::g_serviceLocator->tryGet<Services::Persistence::SettingsStore>()) {
            if (auto blob = store->getSetlist("current"); blob.has_value())
                payload = blob->jsonPayload;
        }
    }
    if (payload.empty()) {
        auto file = getSetPersistenceFile();
        if (!file.existsAsFile()) return;
        auto text = file.loadFileAsString();
        if (text.isEmpty()) return;
        payload = text.toStdString();
    }

    try
    {
        auto j = nlohmann::json::parse(payload);

        if (j.contains("djName")) m_djName = juce::String(std::string(j["djName"]));
        if (j.contains("eventName")) m_eventName = juce::String(std::string(j["eventName"]));
        if (j.contains("eventDate")) m_eventDate = juce::String(std::string(j["eventDate"]));
        if (j.contains("eventVenue")) m_eventVenue = juce::String(std::string(j["eventVenue"]));

        if (m_setNameEditor) m_setNameEditor->setText(m_eventName, false);
        if (m_djEditor)      m_djEditor->setText(m_djName, false);
        if (m_venueEditor)   m_venueEditor->setText(m_eventVenue, false);
        if (m_dateEditor && m_eventDate.isNotEmpty()) m_dateEditor->setText(m_eventDate, false);
        updateHeaderSubtitle();

        m_setlistTable->updateContent();
        updateCompatibility();
        updateTimer();
        updateSetScore();
        refreshStatistics();
    }
    catch (...) {}
}

void SetPreparationView::sortByColumn(SortColumn col)
{
    if (m_sortColumn == col)
        m_sortAscending = !m_sortAscending;
    else
    {
        m_sortColumn = col;
        m_sortAscending = true;
    }

    std::stable_sort(m_tracks.begin(), m_tracks.end(),
        [this](const Models::Track& a, const Models::Track& b) -> bool {
            int cmp = 0;
            switch (m_sortColumn)
            {
            case SortColumn::Number: return false;
            case SortColumn::Title:
                cmp = juce::String(a.title).compareIgnoreCase(juce::String(b.title)); break;
            case SortColumn::Artist:
                cmp = juce::String(a.artist).compareIgnoreCase(juce::String(b.artist)); break;
            case SortColumn::BPM:
                cmp = (a.bpm < b.bpm) ? -1 : (a.bpm > b.bpm ? 1 : 0); break;
            case SortColumn::Key:
                cmp = juce::String(a.camelotKey).compareIgnoreCase(juce::String(b.camelotKey)); break;
            case SortColumn::Energy:
                cmp = (a.energy < b.energy) ? -1 : (a.energy > b.energy ? 1 : 0); break;
            case SortColumn::Duration:
                cmp = (a.duration < b.duration) ? -1 : (a.duration > b.duration ? 1 : 0); break;
            default: break;
            }
            return m_sortAscending ? (cmp < 0) : (cmp > 0);
        });

    m_hasArranged = true;
    m_setlistTable->updateContent();
    updateCompatibility();
    repaint();
}

void SetPreparationView::handleColumnHeaderClick(int columnIndex)
{
    if (columnIndex >= 0 && columnIndex < static_cast<int>(SortColumn::Count))
        sortByColumn(static_cast<SortColumn>(columnIndex));
}

void SetPreparationView::mouseDown(const juce::MouseEvent& e)
{
    // Doit refleter la geometrie de paint() (3 panneaux + header).
    const int W = getWidth();
    const int margin = 16;
    const int headerH = 56;
    const int gap = 8;
    const int panelPad = 14;
    const int leftW  = juce::jmax(240, W / 4);
    const int rightW = juce::jmax(240, W / 4);
    const int centerX = margin + leftW + gap;
    const int contentTop = headerH + kStepsBarH + 6;

    const int cInnerX = centerX + panelPad;
    const int headerY = contentTop + 28 + 30 + 36 + 8;
    const int headerH2 = 16;

    if (e.y >= headerY && e.y < headerY + headerH2 && e.x >= cInnerX)
    {
        static const int colWidths[] = { 26, 184, 124, 56, 40, 78, 44 };
        int colX = cInnerX + 18;
        for (int i = 0; i < 7; ++i)
        {
            if (e.x >= colX && e.x < colX + colWidths[i])
            {
                handleColumnHeaderClick(i);
                return;
            }
            colX += colWidths[i];
        }
    }
}

} // namespace BeatMate::UI
