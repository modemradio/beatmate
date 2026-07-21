#include "SoireePreparationView.h"
#include "../styles/ColorPalette.h"
#include "../widgets/browser/LibraryBrowserPanel.h"
#include "../../services/library/TrackDataProvider.h"
#include "../../services/config/I18n.h"
#include "../../services/persistence/SettingsStore.h"
#include "../../app/ServiceLocator.h"
#include "../utils/ViewPrefs.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <unordered_set>

namespace BeatMate { extern ServiceLocator* g_serviceLocator; }

namespace BeatMate::UI {

static const char* const kCamelotKeys[] = {
    "1A","1B","2A","2B","3A","3B","4A","4B","5A","5B","6A","6B",
    "7A","7B","8A","8B","9A","9B","10A","10B","11A","11B","12A","12B"
};

static int camelotIndex(const juce::String& key)
{
    for (int i = 0; i < 24; ++i)
        if (key.equalsIgnoreCase(kCamelotKeys[i])) return i;
    return -1;
}

static bool areCamelotCompatible(const juce::String& a, const juce::String& b)
{
    int ia = camelotIndex(a), ib = camelotIndex(b);
    if (ia < 0 || ib < 0) return true; // unknown = ok
    int numA = (ia / 2) + 1, numB = (ib / 2) + 1;
    bool modeA = (ia % 2 == 1), modeB = (ib % 2 == 1); // B=major, A=minor
    if (numA == numB) return true; // same number
    if (numA == numB && modeA != modeB) return true; // mode switch
    int diff = std::abs(numA - numB);
    if (diff == 1 || diff == 11) return (modeA == modeB); // adjacent same mode
    return false;
}

int SoireePreparationView::TimelineComponent::getPhaseAtX(float x)
{
    if (!phases || phases->empty()) return -1;
    double totalDur = 0;
    for (auto& p : *phases) totalDur += p.durationMin;
    if (totalDur <= 0) return -1;

    float px = 8.0f;
    float totalW = (float)getWidth() - 16.0f;
    for (int i = 0; i < (int)phases->size(); ++i)
    {
        float phaseW = (float)((*phases)[i].durationMin / totalDur) * totalW;
        if (x >= px && x < px + phaseW)
            return i;
        px += phaseW;
    }
    return -1;
}

bool SoireePreparationView::TimelineComponent::isNearBorder(float x, int& borderPhaseIndex)
{
    if (!phases || phases->size() < 2) return false;
    double totalDur = 0;
    for (auto& p : *phases) totalDur += p.durationMin;
    if (totalDur <= 0) return false;

    float px = 8.0f;
    float totalW = (float)getWidth() - 16.0f;
    for (int i = 0; i < (int)phases->size() - 1; ++i)
    {
        float phaseW = (float)((*phases)[i].durationMin / totalDur) * totalW;
        px += phaseW;
        if (std::abs(x - px) < 15.0f)
        {
            borderPhaseIndex = i;
            return true;
        }
    }
    return false;
}

void SoireePreparationView::TimelineComponent::paint(juce::Graphics& g)
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

    if (!phases || phases->empty())
    {
        g.setFont(juce::Font(13.0f));
        g.setColour(Colors::textMuted());
        g.drawText(juce::String::fromUTF8("Aucune phase - cliquez 'Nouveau' pour creer un evenement"),
                   b, juce::Justification::centred);
        return;
    }

    double totalDur = 0;
    for (auto& p : *phases) totalDur += p.durationMin;
    if (totalDur <= 0) totalDur = 1;

    float px = 8.0f;
    float barY = 24.0f;
    float barH = b.getHeight() - 48.0f;
    float totalW = b.getWidth() - 16.0f;

    g.setColour(Colors::warning().withAlpha(0.6f));
    g.fillRoundedRectangle(8.0f, 6.0f, 3.0f, 12.0f, 1.5f);
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.setColour(Colors::textSecondary());
    g.drawText(juce::String::fromUTF8("DEROULE DE LA SOIREE"), 16.0f, 4.0f, 200.0f, 16.0f, juce::Justification::centredLeft);

    g.setFont(juce::Font(9.0f));
    g.setColour(Colors::textDim());
    double cumMin = 0;
    float cumPx = 8.0f;
    for (int i = 0; i <= (int)phases->size(); ++i)
    {
        int hours = (int)(cumMin / 60);
        int mins = (int)cumMin % 60;
        juce::String timeStr = juce::String(hours) + "h" + (mins < 10 ? "0" : "") + juce::String(mins);
        g.drawText(timeStr, (int)(cumPx - 14), (int)(barY + barH + 4), 34, 14, juce::Justification::centred);
        if (i < (int)phases->size())
        {
            float phaseW = (float)((*phases)[i].durationMin / totalDur) * totalW;
            cumMin += (*phases)[i].durationMin;
            cumPx += phaseW;
        }
    }

    px = 8.0f;
    double startMin = 0.0;
    for (int i = 0; i < (int)phases->size(); ++i)
    {
        auto& phase = (*phases)[i];
        float phaseW = (float)(phase.durationMin / totalDur) * totalW;

        bool isHovered = (i == hoveredPhase);
        bool isSelected = (i == selectedPhase);
        juce::Colour col = phase.color;

        {
            juce::ColourGradient grad(
                col.withAlpha(isSelected ? 0.60f : (isHovered ? 0.38f : 0.25f)),
                px, barY,
                col.withAlpha(isSelected ? 0.30f : (isHovered ? 0.15f : 0.08f)),
                px, barY + barH, false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(px, barY, phaseW - 2.0f, barH, 8.0f);
        }

        if (isSelected)
        {
            g.setColour(Colors::primary());
            g.drawRoundedRectangle(px, barY, phaseW - 2.0f, barH, 8.0f, 2.5f);
            g.setColour(Colors::primary().withAlpha(0.12f));
            g.drawRoundedRectangle(px - 1.0f, barY - 1.0f, phaseW, barH + 2.0f, 9.0f, 3.0f);
        }
        else
        {
            g.setColour(col.withAlpha(0.55f));
            g.drawRoundedRectangle(px, barY, phaseW - 2.0f, barH, 8.0f, 2.0f);
        }

        {
            float eNorm = phase.energyTarget / 10.0f;
            float eBarW = (phaseW - 10.0f) * eNorm;
            juce::Colour eCol = eNorm < 0.35f ? Colors::success()
                               : eNorm < 0.65f ? Colors::warning()
                               : Colors::error();
            g.setColour(Colors::bgLightest().withAlpha(0.35f));
            g.fillRoundedRectangle(px + 4, barY + barH - 14.0f, phaseW - 10.0f, 7.0f, 3.5f);
            {
                juce::ColourGradient eGrad(eCol.withAlpha(0.9f), px + 4, 0,
                                            eCol.brighter(0.3f).withAlpha(0.7f), px + 4 + eBarW, 0, false);
                g.setGradientFill(eGrad);
                g.fillRoundedRectangle(px + 4, barY + barH - 14.0f, eBarW, 7.0f, 3.5f);
            }
        }

        auto fmtTime = [](double minutes) -> juce::String
        {
            int h = (int)(minutes / 60.0);
            int m = (int)minutes % 60;
            return juce::String(h) + "h" + (m < 10 ? "0" : "") + juce::String(m);
        };
        double endMin = startMin + phase.durationMin;

        if (phaseW > 50)
        {
            g.setFont(juce::Font(13.0f, juce::Font::bold));
            g.setColour(Colors::textPrimary());
            g.drawText(phase.name, (int)(px + 8), (int)(barY + 5), (int)(phaseW - 16), 18, juce::Justification::centredLeft);

            g.setFont(juce::Font(10.0f, juce::Font::bold));
            g.setColour(Colors::textSecondary());
            juce::String rangeStr = fmtTime(startMin) + juce::String::fromUTF8(" - ") + fmtTime(endMin);
            if (phase.genre.isNotEmpty()) rangeStr += juce::String::fromUTF8("  ") + phase.genre;
            g.drawText(rangeStr, (int)(px + 8), (int)(barY + 24), (int)(phaseW - 16), 14, juce::Justification::centredLeft);

            if (phaseW > 80)
                ProDraw::badge(g, juce::String(phase.targetBPM, 0) + " BPM",
                               px + 8.0f, barY + 40.0f, 58.0f, 16.0f, Colors::bpmBadge());

            g.setFont(juce::Font(9.0f));
            g.setColour(Colors::textMuted());
            g.drawText(juce::String((int)phase.durationMin) + " min",
                       (int)(px + 8), (int)(barY + 58), (int)(phaseW - 16), 14, juce::Justification::centredLeft);

            int tc = (int)phase.assignedTracks.size();
            if (tc > 0)
                ProDraw::badge(g, juce::String(tc) + (tc > 1 ? " pistes" : " piste"),
                               px + phaseW - 60.0f, barY + 5.0f, 52.0f, 16.0f, Colors::success());
        }

        if (i < (int)phases->size() - 1)
        {
            float borderX = px + phaseW - 1.0f;
            bool hot = (i == hoveredPhase || i == dragBorderPhase || i + 1 == hoveredPhase);
            g.setColour(hot ? Colors::primary().withAlpha(0.9f) : Colors::textSecondary().withAlpha(0.7f));
            for (float dy = barY + 6.0f; dy < barY + barH - 6.0f; dy += 4.0f)
                g.fillRect(borderX - 0.5f, dy, 3.0f, 2.0f);
            g.setColour(hot ? Colors::primary() : Colors::textSecondary().withAlpha(0.85f));
            g.fillRoundedRectangle(borderX - 2.0f, barY + barH * 0.5f - 8.0f, 5.0f, 16.0f, 2.5f);
        }

        px += phaseW;
        startMin = endMin;
    }
}

void SoireePreparationView::TimelineComponent::mouseMove(const juce::MouseEvent& e)
{
    if (!phases || phases->empty()) return;

    int borderIdx = -1;
    if (isNearBorder((float)e.x, borderIdx))
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);

    int newHover = getPhaseAtX((float)e.x);
    if (newHover != hoveredPhase) { hoveredPhase = newHover; repaint(); }
}

void SoireePreparationView::TimelineComponent::mouseDown(const juce::MouseEvent& e)
{
    int borderIdx = -1;
    if (isNearBorder((float)e.x, borderIdx))
    {
        dragBorderPhase = borderIdx;
        dragStartX = (float)e.x;
        dragStartDuration = (*phases)[borderIdx].durationMin;
        dragNextStartDuration = (*phases)[borderIdx + 1].durationMin;
    }
    else
    {
        dragBorderPhase = -1;
        int idx = getPhaseAtX((float)e.x);
        if (idx >= 0)
        {
            selectedPhase = idx;
            if (onPhaseSelected) onPhaseSelected(selectedPhase);
            repaint();
        }
    }
}

void SoireePreparationView::TimelineComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (dragBorderPhase < 0 || !phases) return;

    double totalDur = 0;
    for (auto& p : *phases) totalDur += p.durationMin;
    float totalW = (float)getWidth() - 16.0f;
    double minPerPx = totalDur / totalW;

    float dx = (float)e.x - dragStartX;
    double deltaDur = dx * minPerPx;

    double newDur = juce::jmax(10.0, dragStartDuration + deltaDur);
    double newNextDur = juce::jmax(10.0, dragNextStartDuration - deltaDur);

    (*phases)[dragBorderPhase].durationMin = newDur;
    (*phases)[dragBorderPhase + 1].durationMin = newNextDur;
    repaint();
    if (onPhasesChanged) onPhasesChanged();
}

void SoireePreparationView::TimelineComponent::mouseUp(const juce::MouseEvent&)
{
    dragBorderPhase = -1;
}

void SoireePreparationView::PhaseTrackListModel::paintRowBackground(
    juce::Graphics& g, int row, int w, int h, bool selected)
{
    if (selected)
    {
        g.setColour(Colors::primary().withAlpha(0.18f));
        g.fillRect(0, 0, w, h);
        g.setColour(Colors::primary().withAlpha(0.7f));
        g.fillRect(0, 0, 3, h);
    }
    else if (row % 2)
    {
        g.fillAll(Colors::bgCard().withAlpha(0.18f));
    }
}

juce::String SoireePreparationView::PhaseTrackListModel::getCellText(int row, int col)
{
    if (!tracks || row < 0 || row >= (int)tracks->size()) return {};
    auto& t = (*tracks)[row];
    switch (col)
    {
        case 0: return juce::String(row + 1);
        case 1: return juce::String(t.title);
        case 2: return juce::String(t.artist);
        case 3: return t.bpm > 0 ? juce::String(t.bpm, 1) : juce::String("-");
        case 4: return t.camelotKey.empty() ? juce::String(t.key) : juce::String(t.camelotKey);
        case 5: return t.energy > 0 ? juce::String(t.energy, 0) : juce::String("-");
        case 6: {
            int mins = (int)(t.duration / 60.0);
            int secs = (int)t.duration % 60;
            return juce::String(mins) + ":" + (secs < 10 ? "0" : "") + juce::String(secs);
        }
        case 7: {
            if (row == 0 || !tracks) return "-";
            auto& prev = (*tracks)[row - 1];
            int score = 0;
            if (prev.bpm > 0 && t.bpm > 0)
            {
                double bpmDiff = std::abs(t.bpm - prev.bpm);
                score += juce::jmax(0, 40 - (int)(bpmDiff * 4));
            }
            juce::String ka = prev.camelotKey.empty() ? juce::String(prev.key) : juce::String(prev.camelotKey);
            juce::String kb = t.camelotKey.empty() ? juce::String(t.key) : juce::String(t.camelotKey);
            if (areCamelotCompatible(ka, kb)) score += 30;
            float eDiff = std::abs(t.energy - prev.energy);
            score += juce::jmax(0, 30 - (int)(eDiff * 10));
            return juce::String(score) + "%";
        }
        default: return {};
    }
}

void SoireePreparationView::PhaseTrackListModel::paintCell(
    juce::Graphics& g, int row, int col, int w, int h, bool selected)
{
    juce::String text = getCellText(row, col);
    g.setFont(juce::Font(11.0f));

    if (col == 3) // BPM - pill bleue (meme badge que Set Prep)
    {
        ProDraw::badge(g, text, 4.0f, 4.0f, (float)(w - 8), (float)(h - 8), Colors::bpmBadge());
    }
    else if (col == 4) // Cle - pill violette
    {
        ProDraw::badge(g, text, 4.0f, 4.0f, (float)(w - 8), (float)(h - 8), Colors::keyBadge());
    }
    else if (col == 5) // Energy - mini progress bar
    {
        if (tracks && row >= 0 && row < (int)tracks->size())
        {
            float eVal = (*tracks)[row].energy;
            float eNorm = juce::jlimit(0.0f, 1.0f, eVal / 10.0f);
            float barX = 4.0f;
            float barY = (float)(h / 2 - 4);
            float barW = (float)(w - 28);
            g.setColour(Colors::bgLightest().withAlpha(0.6f));
            g.fillRoundedRectangle(barX, barY, barW, 8.0f, 4.0f);
            juce::Colour eCol = eNorm < 0.35f ? Colors::success()
                               : eNorm < 0.65f ? Colors::warning()
                               : Colors::error();
            g.setColour(eCol);
            g.fillRoundedRectangle(barX, barY, barW * eNorm, 8.0f, 4.0f);
            g.setFont(juce::Font(9.0f, juce::Font::bold));
            g.setColour(eCol);
            g.drawText(eVal > 0 ? text : "-", (int)(barX + barW + 2), 0, 20, h, juce::Justification::centredLeft);
        }
    }
    else if (col == 7 && row > 0) // Score de compatibilite (pill coloree)
    {
        int score = text.trimCharactersAtEnd("%").getIntValue();
        juce::Colour scoreCol = score >= 70 ? Colors::success()
                               : score >= 40 ? Colors::warning()
                               : Colors::error();
        ProDraw::badge(g, text, 4.0f, 4.0f, (float)(w - 8), (float)(h - 8), scoreCol);
    }
    else if (col == 0)
    {
        g.setColour(Colors::textMuted());
        g.drawText(text, 4, 0, w - 8, h, juce::Justification::centredLeft);
    }
    else if (col == 2) // Artist - textSecondary not textMuted
    {
        g.setColour(Colors::textSecondary());
        g.drawText(text, 4, 0, w - 8, h, juce::Justification::centredLeft);
    }
    else
    {
        g.setColour(selected ? Colors::textPrimary() : Colors::textSecondary());
        g.drawText(text, 4, 0, w - 8, h, juce::Justification::centredLeft);
    }

    g.setColour(Colors::border().withAlpha(0.10f));
    g.drawVerticalLine(w - 1, 0.0f, (float)h);
}

void SoireePreparationView::ChecklistComponent::paint(juce::Graphics& g)
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
    g.drawText("CHECKLIST TECHNIQUE", 16.0f, 4.0f, 200.0f, 16.0f, juce::Justification::centredLeft);

    float y = 28.0f;
    g.setFont(juce::Font(11.0f));
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

void SoireePreparationView::ChecklistComponent::mouseDown(const juce::MouseEvent& e)
{
    // Must match paint() layout: items drawn starting at y=28, rowH=30
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

static juce::File getChecklistPersistenceFile()
{
    auto appDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                      .getChildFile("BeatMate");
    if (!appDir.isDirectory()) appDir.createDirectory();
    return appDir.getChildFile("soiree_checklist.json");
}

static void saveChecklistToJSON(const SoireePreparationView::Soiree& soiree)
{
    nlohmann::json j;
    j["sonoOK"] = soiree.sonoOK;
    j["lumieresOK"] = soiree.lumieresOK;
    j["riderOK"] = soiree.riderOK;
    j["backupUSB"] = soiree.backupUSB;
    j["setlistPrinted"] = soiree.setlistPrinted;
    const std::string dumped = j.dump(2);

    bool dbOk = false;
    if (BeatMate::g_serviceLocator) {
        if (auto* store = BeatMate::g_serviceLocator->tryGet<Services::Persistence::SettingsStore>()) {
            dbOk = store->upsertEventPlan("checklist", dumped);
        }
    }
    auto file = getChecklistPersistenceFile();
    file.replaceWithText(juce::String(dumped));
    if (!dbOk)
        spdlog::warn("[Soiree] saveChecklistToJSON: DB write failed (JSON fallback only)");
}

static void loadChecklistFromJSON(SoireePreparationView::Soiree& soiree)
{
    std::string payload;
    if (BeatMate::g_serviceLocator) {
        if (auto* store = BeatMate::g_serviceLocator->tryGet<Services::Persistence::SettingsStore>()) {
            if (auto blob = store->getEventPlan("checklist"); blob.has_value())
                payload = blob->jsonPayload;
        }
    }
    if (payload.empty()) {
        auto file = getChecklistPersistenceFile();
        if (!file.existsAsFile()) return;
        payload = file.loadFileAsString().toStdString();
    }
    try
    {
        auto j = nlohmann::json::parse(payload);
        soiree.sonoOK = j.value("sonoOK", false);
        soiree.lumieresOK = j.value("lumieresOK", false);
        soiree.riderOK = j.value("riderOK", false);
        soiree.backupUSB = j.value("backupUSB", false);
        soiree.setlistPrinted = j.value("setlistPrinted", false);
    }
    catch (...) {}
}

void SoireePreparationView::SoireeContentComponent::paint(juce::Graphics& g)
{
    if (!owner) return;

    int W = getWidth();
    int H = getHeight();
    int margin = 20;
    int gap = 8;

    int headerH = 0; // content is already below the header
    int contentTop = 6;
    int parentH = owner->getHeight();
    int bottomH = 52;
    int contentH = parentH - 56 - bottomH - 14; // match resized() logic

    int leftW = juce::jmax(240, W / 4);
    int rightW = juce::jmax(240, W / 4);
    int centerW = W - leftW - rightW - margin * 2 - gap * 2;
    int centerX = margin + leftW + gap;
    int rightX = W - rightW - margin;

    auto drawGlassPanel = [&](juce::Rectangle<int> r)
    {
        ProDraw::glassPanel(g, r.toFloat(), 12.0f);
    };

    auto drawSectionLabel = [&](const juce::String& text, int x, int y, juce::Colour accentCol)
    {
        ProDraw::sectionHeader(g, text, x, y, accentCol);
    };

    drawGlassPanel({margin, contentTop, leftW, contentH});
    drawSectionLabel(juce::String::fromUTF8("INFOS EVENEMENT"), margin + 10, contentTop + 8, Colors::primary());
    drawSectionLabel(juce::String::fromUTF8("PROFIL D'ENERGIE"), margin + 10, contentTop + 178, Colors::success());
    drawSectionLabel(juce::String::fromUTF8("TIMELINE DES PHASES"), margin + 10, contentTop + 218, Colors::warning());

    drawGlassPanel({centerX, contentTop, centerW, contentH});
    drawSectionLabel(juce::String::fromUTF8("PHASES & MORCEAUX"), centerX + 10, contentTop + 8, Colors::accent());

    drawGlassPanel({rightX, contentTop, rightW, contentH});
    drawSectionLabel(juce::String::fromUTF8("CHECKLIST & EXPORT"), rightX + 10, contentTop + 8, Colors::secondary());
}

SoireePreparationView::SoireePreparationView()
    : m_provider(nullptr)
{
    setupUI();
}

SoireePreparationView::SoireePreparationView(Services::Library::TrackDataProvider* provider)
    : m_provider(provider)
{
    setupUI();

    m_phaseTimeline = std::make_unique<Widgets::PhaseTimelineWidget>();
    m_phaseTimeline->setEventDurationMinutes(300.0);
    if (m_provider) {
        m_phaseTimeline->setTrackLookup([this](int64_t id) {
            return m_provider->getTrack(id);
        });
    }
    m_phaseTimeline->setVisible(false);
    addChildComponent(*m_phaseTimeline);

    m_handoverList = std::make_unique<Widgets::HandoverListWidget>();
    m_handoverList->setVisible(false);
    addChildComponent(*m_handoverList);
}

void SoireePreparationView::setupUI()
{
    m_content = std::make_unique<SoireeContentComponent>();
    m_content->owner = this;
    m_viewport = std::make_unique<juce::Viewport>();
    m_viewport->setViewedComponent(m_content.get(), false);
    m_viewport->setScrollBarsShown(true, false);
    m_viewport->setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::nonHover);
    m_viewport->getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId, Colors::primary().withAlpha(0.5f));
    m_viewport->getVerticalScrollBar().setColour(juce::ScrollBar::trackColourId, Colors::bgDarkest());
    addAndMakeVisible(*m_viewport);

    auto* c = m_content.get();

    auto makeBtn = [c](const juce::String& text, juce::Colour bg = Colors::bgLighter())
    {
        auto b = std::make_unique<juce::TextButton>(text);
        b->setColour(juce::TextButton::buttonColourId, bg);
        b->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        c->addAndMakeVisible(*b);
        return b;
    };

    auto makeLabel = [c](const juce::String& text)
    {
        auto l = std::make_unique<juce::Label>("", text);
        l->setFont(juce::Font(11.0f));
        l->setColour(juce::Label::textColourId, Colors::textMuted());
        c->addAndMakeVisible(*l);
        return l;
    };

    auto makeEditor = [c](const juce::String& placeholder = "")
    {
        auto e = std::make_unique<juce::TextEditor>();
        e->setColour(juce::TextEditor::backgroundColourId, Colors::bgLighter().withAlpha(0.5f));
        e->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
        e->setColour(juce::TextEditor::outlineColourId, Colors::border().withAlpha(0.4f));
        if (placeholder.isNotEmpty())
            e->setTextToShowWhenEmpty(placeholder, Colors::textDim());
        c->addAndMakeVisible(*e);
        return e;
    };

    m_titleLabel = std::make_unique<juce::Label>("t", "");
    m_titleLabel->setFont(juce::Font(22.0f, juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(*m_titleLabel);

    m_newBtn = std::make_unique<juce::TextButton>(juce::String::fromUTF8("Nouveau"));
    m_newBtn->setColour(juce::TextButton::buttonColourId, Colors::primary());
    m_newBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    addAndMakeVisible(*m_newBtn);
    m_newBtn->onClick = [this] { spdlog::info("[SoireePrepView] newSoiree clicked"); onNewSoiree(); };

    m_mySoireesBtn = std::make_unique<juce::TextButton>(juce::String::fromUTF8("Charger"));
    m_mySoireesBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    m_mySoireesBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    addAndMakeVisible(*m_mySoireesBtn);
    m_mySoireesBtn->onClick = [this] { spdlog::info("[SoireePrepView] loadSoiree clicked"); onLoadSoiree(); };

    m_nameLabel = makeLabel(BM_TJ("soiree.eventName") + ":");
    m_venueLabel = makeLabel(BM_TJ("soiree.venue") + ":");
    m_dateLabel = makeLabel(BM_TJ("soiree.date") + ":");
    m_typeLabel = makeLabel(BM_TJ("soiree.crowd") + ":");
    m_durationLabel = makeLabel(BM_TJ("setPrep.duration") + ":");

    m_nameEditor = makeEditor("Soiree Club Paradise");
    m_venueEditor = makeEditor("Le Warehouse");
    m_dateEditor = makeEditor("2026-04-15");

    m_nameEditor->onTextChange  = [this] { m_soiree.name  = m_nameEditor->getText();  repaint(); };
    m_venueEditor->onTextChange = [this] { m_soiree.venue = m_venueEditor->getText(); repaint(); };
    m_dateEditor->onTextChange  = [this] { m_soiree.date  = m_dateEditor->getText();  repaint(); };

    m_typeCombo = std::make_unique<juce::ComboBox>("type");
    m_typeCombo->addItem(BM_TJ("soiree.settings.type.club"), 1);
    m_typeCombo->addItem(BM_TJ("soiree.settings.type.lounge"), 2);
    m_typeCombo->addItem(BM_TJ("soiree.settings.type.festival"), 3);
    m_typeCombo->addItem(BM_TJ("soiree.settings.type.wedding"), 4);
    m_typeCombo->addItem(BM_TJ("soiree.settings.type.corporate"), 5);
    m_typeCombo->addItem(BM_TJ("soiree.settings.type.afterparty"), 6);
    m_typeCombo->addItem(BM_TJ("soiree.settings.type.birthday"), 7);
    m_typeCombo->setSelectedId(1, juce::dontSendNotification);
    m_typeCombo->setTooltip(BM_TJ("soiree.settings.typeTip"));
    m_typeCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter().withAlpha(0.5f));
    m_typeCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_typeCombo->setColour(juce::ComboBox::outlineColourId, Colors::border().withAlpha(0.4f));
    m_typeCombo->onChange = [this]
    {
        m_soiree.typeIndex = m_typeCombo->getSelectedId() - 1;
        createDefaultPhases(m_soiree.typeIndex);
        rebuildTimeline();
        Prefs::setInt("soiree.typeId", m_typeCombo->getSelectedId());
    };
    c->addAndMakeVisible(*m_typeCombo);

    m_durationSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    m_durationSlider->setRange(1, 12, 0.5);
    m_durationSlider->setValue(5.0);
    m_durationSlider->setTextValueSuffix("h");
    m_durationSlider->setColour(juce::Slider::backgroundColourId, Colors::bgLightest());
    m_durationSlider->setColour(juce::Slider::trackColourId, Colors::primary());
    m_durationSlider->setColour(juce::Slider::thumbColourId, Colors::primary());
    m_durationSlider->setColour(juce::Slider::textBoxTextColourId, Colors::textPrimary());
    m_durationSlider->setColour(juce::Slider::textBoxBackgroundColourId, Colors::bgLighter().withAlpha(0.5f));
    m_durationSlider->setColour(juce::Slider::textBoxOutlineColourId, Colors::border().withAlpha(0.4f));
    m_durationSlider->onValueChange = [this]
    {
        m_soiree.totalDurationHours = m_durationSlider->getValue();
        Prefs::setDouble("soiree.durationHours", m_durationSlider->getValue());
    };
    c->addAndMakeVisible(*m_durationSlider);

    m_profileLabel = makeLabel(BM_TJ("setPrep.energy") + ":");
    m_profileCombo = std::make_unique<juce::ComboBox>("profile");
    m_profileCombo->addItem(BM_TJ("soiree.settings.profile.classic"), 1);
    m_profileCombo->addItem(BM_TJ("soiree.settings.profile.doubleDrop"), 2);
    m_profileCombo->addItem(BM_TJ("soiree.settings.profile.marathon"), 3);
    m_profileCombo->addItem(BM_TJ("soiree.settings.profile.progressive"), 4);
    m_profileCombo->addItem(BM_TJ("soiree.settings.profile.wedding"), 5);
    m_profileCombo->setTooltip(BM_TJ("soiree.settings.profileTip"));
    m_profileCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter().withAlpha(0.5f));
    m_profileCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_profileCombo->setColour(juce::ComboBox::outlineColourId, Colors::border().withAlpha(0.4f));
    m_profileCombo->onChange = [this]
    {
        int idx = m_profileCombo->getSelectedId() - 1;
        if (idx >= 0) applyEnergyProfile(idx);
        Prefs::setInt("soiree.profileId", m_profileCombo->getSelectedId());
    };
    c->addAndMakeVisible(*m_profileCombo);

    m_presets = {
        {"Classic DJ Set",  {3, 5, 7, 9, 7, 4}},
        {"Double Drop",     {4, 8, 5, 9, 6, 3}},
        {"Marathon",        {5, 6, 7, 8, 8, 9, 8, 7, 5}},
        {"Progressive",     {2, 3, 5, 6, 8, 10}},
        {"Wedding",         {3, 2, 4, 8, 9, 6, 3}},
    };

    m_timeline = std::make_unique<TimelineComponent>();
    m_timeline->phases = &m_soiree.phases;
    m_timeline->onPhaseSelected = [this](int idx) { selectPhase(idx); };
    m_timeline->onPhasesChanged = [this] { updatePhaseEditor(); };
    c->addAndMakeVisible(*m_timeline);

    m_phaseNameLabel   = makeLabel("Nom:");
    m_phaseGenreLabel  = makeLabel("Genre:");
    m_phaseBpmLabel    = makeLabel("BPM cible:");
    m_phaseEnergyLabel = makeLabel("Energie:");
    m_phaseDurLabel    = makeLabel("Duree:");
    m_phaseColorLabel  = makeLabel("Couleur:");

    m_phaseNameEditor = makeEditor("Nom de la phase");
    m_phaseNameEditor->onTextChange = [this] { syncEditorToPhase(); };

    m_phaseGenreCombo = std::make_unique<juce::ComboBox>("pg");
    for (auto& g : {"House", "Techno", "Deep House", "Tech House", "Trance",
                     "EDM", "Drum & Bass", "Hip-Hop", "R&B", "Pop", "Disco",
                     "Funk", "Afrobeat", "Reggaeton", "Latin", "Dancehall",
                     "Ambient", "Chill", "Jazz", "Soul", "Rock", "Electro"})
        m_phaseGenreCombo->addItem(g, m_phaseGenreCombo->getNumItems() + 1);
    m_phaseGenreCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter().withAlpha(0.5f));
    m_phaseGenreCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_phaseGenreCombo->setColour(juce::ComboBox::outlineColourId, Colors::border().withAlpha(0.4f));
    m_phaseGenreCombo->setEditableText(true);
    m_phaseGenreCombo->onChange = [this] { syncEditorToPhase(); };
    c->addAndMakeVisible(*m_phaseGenreCombo);

    auto makePhaseSlider = [c](double lo, double hi, double step, const juce::String& suffix, juce::Colour trackCol)
    {
        auto s = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
        s->setRange(lo, hi, step);
        s->setTextValueSuffix(suffix);
        s->setColour(juce::Slider::backgroundColourId, Colors::bgLightest());
        s->setColour(juce::Slider::trackColourId, trackCol);
        s->setColour(juce::Slider::thumbColourId, trackCol);
        s->setColour(juce::Slider::textBoxTextColourId, Colors::textPrimary());
        s->setColour(juce::Slider::textBoxBackgroundColourId, Colors::bgLighter().withAlpha(0.5f));
        s->setColour(juce::Slider::textBoxOutlineColourId, Colors::border().withAlpha(0.4f));
        c->addAndMakeVisible(*s);
        return s;
    };

    m_phaseBpmSlider = makePhaseSlider(80, 180, 1, " BPM", Colors::accent());
    m_phaseBpmSlider->onValueChange = [this] { syncEditorToPhase(); };

    m_phaseEnergySlider = makePhaseSlider(1, 10, 1, "", Colors::warning());
    m_phaseEnergySlider->onValueChange = [this] { syncEditorToPhase(); };

    m_phaseDurSlider = makePhaseSlider(10, 240, 5, " min", Colors::success());
    m_phaseDurSlider->onValueChange = [this] { syncEditorToPhase(); };

    m_phaseColorBtn = makeBtn(juce::String::fromUTF8("Couleur"), Colors::secondary());
    m_phaseColorBtn->onClick = [this]
    {
        if (m_selectedPhaseIndex < 0 || m_selectedPhaseIndex >= (int)m_soiree.phases.size()) return;
        static const juce::Colour presetColors[] = {
            juce::Colour(0xFF3B82F6), juce::Colour(0xFF8B5CF6), juce::Colour(0xFF06B6D4),
            juce::Colour(0xFF10B981), juce::Colour(0xFFF59E0B), juce::Colour(0xFFEF4444),
            juce::Colour(0xFFF472B6), juce::Colour(0xFFEC4899), juce::Colour(0xFF22C55E),
        };
        auto& phase = m_soiree.phases[m_selectedPhaseIndex];
        int cur = 0;
        for (int i = 0; i < 9; ++i)
            if (phase.color == presetColors[i]) { cur = i; break; }
        phase.color = presetColors[(cur + 1) % 9];
        m_timeline->repaint();
    };

    m_autoFillBtn = makeBtn("IA Auto-Remplir", Colors::secondary());
    m_autoFillBtn->onClick = [this] { spdlog::info("[SoireePrepView] autoFillAI clicked"); onAutoFillAI(); };

    m_addPhaseBtn = makeBtn(juce::String::fromUTF8("+ Ajouter une phase"), Colors::success());
    m_addPhaseBtn->onClick = [this]
    {
        static const juce::Colour presetColors[] = {
            juce::Colour(0xFF3B82F6), juce::Colour(0xFF8B5CF6), juce::Colour(0xFF06B6D4),
            juce::Colour(0xFF10B981), juce::Colour(0xFFF59E0B), juce::Colour(0xFFEF4444),
            juce::Colour(0xFFF472B6), juce::Colour(0xFFEC4899), juce::Colour(0xFF22C55E),
        };
        Phase newPhase;
        if (!m_soiree.phases.empty())
        {
            const auto& last = m_soiree.phases.back();
            newPhase.genre       = last.genre;
            newPhase.targetBPM   = last.targetBPM;
            newPhase.energyTarget= last.energyTarget;
            newPhase.durationMin = last.durationMin;
            newPhase.color       = presetColors[m_soiree.phases.size() % 9];
            newPhase.name        = "Phase " + juce::String((int)m_soiree.phases.size() + 1);
        }
        else
        {
            newPhase.name        = "Phase 1";
            newPhase.genre       = "House";
            newPhase.targetBPM   = 128.0f;
            newPhase.energyTarget= 5;
            newPhase.durationMin = 30.0;
            newPhase.color       = presetColors[0];
        }
        m_soiree.phases.push_back(newPhase);
        rebuildTimeline();
        selectPhase((int)m_soiree.phases.size() - 1);
    };

    m_removePhaseBtn = makeBtn(juce::String::fromUTF8("- Retirer la phase"), Colors::error());
    m_removePhaseBtn->onClick = [this]
    {
        if (m_soiree.phases.size() <= 1 || m_selectedPhaseIndex < 0
            || m_selectedPhaseIndex >= (int)m_soiree.phases.size())
            return;
        m_soiree.phases.erase(m_soiree.phases.begin() + m_selectedPhaseIndex);
        int newSel = juce::jmax(0, m_selectedPhaseIndex - 1);
        rebuildTimeline();
        selectPhase(newSel);
    };

    m_phaseTrackModel = std::make_unique<PhaseTrackListModel>();
    m_phaseTrackList = std::make_unique<juce::TableListBox>("ptl", m_phaseTrackModel.get());
    m_phaseTrackList->setColour(juce::ListBox::backgroundColourId, Colors::bgDark().withAlpha(0.4f));
    m_phaseTrackList->setColour(juce::ListBox::outlineColourId, Colors::border().withAlpha(0.3f));
    m_phaseTrackList->setRowHeight(28);
    m_phaseTrackList->getHeader().setColour(juce::TableHeaderComponent::backgroundColourId, Colors::bgMedium().withAlpha(0.5f));
    m_phaseTrackList->getHeader().setColour(juce::TableHeaderComponent::textColourId, Colors::textSecondary());
    m_phaseTrackList->getHeader().addColumn("#",      1, 30,  30,  30);
    m_phaseTrackList->getHeader().addColumn("Titre",  2, 180, 80,  400);
    m_phaseTrackList->getHeader().addColumn("Artiste",3, 140, 60,  300);
    m_phaseTrackList->getHeader().addColumn("BPM",    4, 55,  40,  80);
    m_phaseTrackList->getHeader().addColumn("Key",    5, 50,  40,  70);
    m_phaseTrackList->getHeader().addColumn("Energy", 6, 50,  40,  70);
    m_phaseTrackList->getHeader().addColumn("Duree",  7, 55,  40,  80);
    m_phaseTrackList->getHeader().addColumn("Score",  8, 55,  40,  80);
    c->addAndMakeVisible(*m_phaseTrackList);

    m_addTrackBtn = makeBtn(juce::String::fromUTF8("+ Suggerer (IA)"), Colors::success().darker(0.3f));
    m_addTrackBtn->onClick = [this]
    {
        if (m_selectedPhaseIndex < 0)
        {
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::InfoIcon)
                    .withTitle("Aucune phase selectionnee")
                    .withMessage("Selectionne une phase a gauche avant d'ajouter des morceaux.")
                    .withButton("OK"),
                nullptr);
            return;
        }
        if (!m_provider)
        {
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::WarningIcon)
                    .withTitle("Bibliotheque indisponible")
                    .withMessage("Aucune source de pistes n'est connectee.")
                    .withButton("OK"),
                nullptr);
            return;
        }
        auto tracks = findMatchingTracks(m_selectedPhaseIndex);
        if (!tracks.empty())
        {
            auto& phase = m_soiree.phases[m_selectedPhaseIndex];
            int added = 0;
            for (auto& t : tracks)
            {
                bool dup = false;
                for (auto& ex : phase.assignedTracks)
                    if (ex.id == t.id) { dup = true; break; }
                if (!dup) { phase.assignedTracks.push_back(t); ++added; }
            }
            updatePhaseTrackList();
            if (m_timeline) m_timeline->repaint();
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::InfoIcon)
                    .withTitle("Morceaux ajoutes")
                    .withMessage(juce::String(added) + " morceau(x) ajoute(s) a la phase.")
                    .withButton("OK"),
                nullptr);
        }
        else
        {
            if (m_libraryBrowser)
            {
                m_libraryBrowser->setVisible(true);
                m_libraryBrowser->toFront(true);
                m_libraryBrowser->refreshResults();
                resized();
            }
        }
    };

    m_removeTrackBtn = makeBtn(juce::String::fromUTF8("- Retirer le morceau"), Colors::error().darker(0.3f));
    m_removeTrackBtn->onClick = [this]
    {
        if (m_selectedPhaseIndex < 0 || m_selectedPhaseIndex >= (int)m_soiree.phases.size()) return;
        int sel = m_phaseTrackList->getSelectedRow();
        auto& tracks = m_soiree.phases[m_selectedPhaseIndex].assignedTracks;
        if (sel >= 0 && sel < (int)tracks.size())
        {
            tracks.erase(tracks.begin() + sel);
            updatePhaseTrackList();
            m_timeline->repaint();
        }
    };

    m_clearTracksBtn = makeBtn(juce::String::fromUTF8("Vider tout"), Colors::error());
    m_clearTracksBtn->onClick = [this]
    {
        bool any = false;
        for (auto& ph : m_soiree.phases)
            if (!ph.assignedTracks.empty()) { any = true; break; }
        if (!any) return;
        juce::Component::SafePointer<SoireePreparationView> self(this);
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon,
            juce::String::fromUTF8("Vider la soiree"),
            juce::String::fromUTF8("Retirer tous les morceaux de toutes les phases ?"),
            juce::String::fromUTF8("Vider"), juce::String::fromUTF8("Annuler"), this,
            juce::ModalCallbackFunction::create([self](int result) {
                if (result != 1 || self == nullptr) return;
                for (auto& ph : self->m_soiree.phases) ph.assignedTracks.clear();
                self->updatePhaseTrackList();
                if (self->m_timeline) self->m_timeline->repaint();
            }));
    };

    m_checklist = std::make_unique<ChecklistComponent>();
    c->addAndMakeVisible(*m_checklist);
    loadChecklistFromJSON(m_soiree);
    m_checklist->items = {
        {"Sono / PA System OK",     &m_soiree.sonoOK},
        {"Lumieres / Lighting OK",  &m_soiree.lumieresOK},
        {"Rider technique OK",      &m_soiree.riderOK},
        {"Backup USB prete",        &m_soiree.backupUSB},
        {"Setlist imprimee",        &m_soiree.setlistPrinted},
        {"Casque + cable RCA",      &m_soiree.headphonesReady},
        {"Controleur MIDI/DJ pret", &m_soiree.controllerReady},
        {"Adaptateurs secteur",     &m_soiree.powerAdaptersReady},
        {"Contact organisateur OK", &m_soiree.contactOK},
        {"Paiement/contrat signe",  &m_soiree.paymentOK},
    };
    m_checklist->onChecklistChanged = [this] { saveChecklistToJSON(m_soiree); };

    m_exportPDFBtn  = makeBtn(juce::String::fromUTF8("Feuille de route (texte)"),  Colors::primary());
    m_exportJSONBtn = makeBtn(juce::String::fromUTF8("Sauvegarder (.json)"), Colors::bgLighter());
    m_exportM3UBtn  = makeBtn(juce::String::fromUTF8("Playlist M3U (USB)"),  Colors::bgLighter());
    m_exportPDFBtn->onClick  = [this] { spdlog::info("[SoireePrepView] exportPDF clicked"); onExportPDF(); };
    m_exportJSONBtn->onClick = [this] { spdlog::info("[SoireePrepView] exportJSON clicked"); onExportJSON(); };
    m_exportM3UBtn->onClick  = [this] { spdlog::info("[SoireePrepView] exportM3U clicked"); onExportM3U(); };

    m_libraryBrowser = std::make_unique<LibraryBrowserPanel>(m_provider);
    m_libraryBrowser->setVisible(false);

    m_browseLibraryBtn = std::make_unique<juce::TextButton>(BM_TJ("library.myLibrary"));
    m_browseLibraryBtn->setColour(juce::TextButton::buttonColourId, Colors::accent().withAlpha(0.6f));
    m_browseLibraryBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_browseLibraryBtn->onClick = [this] { spdlog::info("[SoireePrepView] browseLibrary clicked"); onBrowseLibrary(); };
    c->addAndMakeVisible(*m_browseLibraryBtn);

    struct SoireeLibListener : public LibraryBrowserPanel::Listener
    {
        SoireePreparationView* owner;
        SoireeLibListener(SoireePreparationView* o) : owner(o) {}
        void trackDoubleClicked(const Models::Track& track) override
        {
            if (!owner) return;

            int idx = owner->m_selectedPhaseIndex;
            if (idx < 0 || idx >= (int)owner->m_soiree.phases.size()) {
                if (owner->m_soiree.phases.empty()) {
                    juce::AlertWindow::showAsync(
                        juce::MessageBoxOptions()
                            .withIconType(juce::MessageBoxIconType::InfoIcon)
                            .withTitle("Aucune phase")
                            .withMessage("Cree au moins une phase (ou charge un template) avant d'ajouter des morceaux.")
                            .withButton("OK"),
                        nullptr);
                    return;
                }
                idx = 0;
                owner->m_selectedPhaseIndex = 0;
            }

            auto& phase = owner->m_soiree.phases[idx];
            bool dup = false;
            for (auto& t : phase.assignedTracks)
                if (t.id == track.id) { dup = true; break; }
            if (!dup) {
                phase.assignedTracks.push_back(track);
                owner->updatePhaseTrackList();
                if (owner->m_timeline) owner->m_timeline->repaint();
            }
        }
        void addTrackRequested(const Models::Track& track) override { trackDoubleClicked(track); }
        void tracksDropped(const std::vector<Models::Track>& tracks) override
        {
            for (auto& t : tracks) trackDoubleClicked(t);
        }
    };
    m_libListener = std::make_unique<SoireeLibListener>(this);
    m_libraryBrowser->addListener(m_libListener.get());

    {
        const int typeId = Prefs::getInt("soiree.typeId", 1);
        if (m_typeCombo && typeId >= 1 && typeId <= m_typeCombo->getNumItems())
            m_typeCombo->setSelectedId(typeId, juce::dontSendNotification);
        const int profId = Prefs::getInt("soiree.profileId", 0);
        if (m_profileCombo && profId >= 1 && profId <= m_profileCombo->getNumItems())
            m_profileCombo->setSelectedId(profId, juce::dontSendNotification);
        const double dur = Prefs::getDouble("soiree.durationHours", 5.0);
        if (m_durationSlider) {
            m_durationSlider->setValue(dur, juce::dontSendNotification);
            m_soiree.totalDurationHours = dur;
        }
    }

    retranslateUi();
}

void SoireePreparationView::retranslateUi()
{
    auto rebuildCombo = [](juce::ComboBox* cb, std::initializer_list<const char*> keys)
    {
        if (!cb) return;
        int prev = cb->getSelectedId();
        cb->clear(juce::dontSendNotification);
        int id = 1;
        for (auto k : keys) cb->addItem(BM_TJ(k), id++);
        if (prev > 0)
            cb->setSelectedId(prev, juce::dontSendNotification);
    };

    if (m_nameLabel)     m_nameLabel->setText(BM_TJ("soiree.eventName") + ":", juce::dontSendNotification);
    if (m_venueLabel)    m_venueLabel->setText(BM_TJ("soiree.venue") + ":", juce::dontSendNotification);
    if (m_dateLabel)     m_dateLabel->setText(BM_TJ("soiree.date") + ":", juce::dontSendNotification);
    if (m_typeLabel)     m_typeLabel->setText(BM_TJ("soiree.crowd") + ":", juce::dontSendNotification);
    if (m_durationLabel) m_durationLabel->setText(BM_TJ("setPrep.duration") + ":", juce::dontSendNotification);
    if (m_profileLabel)  m_profileLabel->setText(BM_TJ("setPrep.energy") + ":", juce::dontSendNotification);

    rebuildCombo(m_typeCombo.get(), {
        "soiree.settings.type.club", "soiree.settings.type.lounge",
        "soiree.settings.type.festival", "soiree.settings.type.wedding",
        "soiree.settings.type.corporate", "soiree.settings.type.afterparty",
        "soiree.settings.type.birthday" });
    if (m_typeCombo) m_typeCombo->setTooltip(BM_TJ("soiree.settings.typeTip"));

    rebuildCombo(m_profileCombo.get(), {
        "soiree.settings.profile.classic", "soiree.settings.profile.doubleDrop",
        "soiree.settings.profile.marathon", "soiree.settings.profile.progressive",
        "soiree.settings.profile.wedding" });
    if (m_profileCombo) m_profileCombo->setTooltip(BM_TJ("soiree.settings.profileTip"));

    if (m_browseLibraryBtn) m_browseLibraryBtn->setButtonText(BM_TJ("library.myLibrary"));

    repaint();
}

void SoireePreparationView::createDefaultPhases(int typeIndex)
{
    m_soiree.phases.clear();
    m_selectedPhaseIndex = -1;

    auto addPhase = [this](const juce::String& n, const juce::String& g, float bpm, int energy, double dur, juce::Colour col)
    {
        Phase p;
        p.name = n; p.genre = g; p.targetBPM = bpm; p.energyTarget = energy;
        p.durationMin = dur; p.color = col;
        m_soiree.phases.push_back(p);
    };

    switch (typeIndex)
    {
    case 0: // Club
        addPhase("Warm-up",    "Deep House",  118, 3, 60,  juce::Colour(0xFF3B82F6));
        addPhase("Build",      "House",       124, 6, 60,  juce::Colour(0xFF06B6D4));
        addPhase("Peak",       "Tech House",  128, 9, 120, juce::Colour(0xFFEF4444));
        addPhase("Wind-down",  "Deep House",  120, 4, 60,  juce::Colour(0xFF8B5CF6));
        break;
    case 1: // Lounge
        addPhase("Accueil",    "Chill",       95,  2, 60,  juce::Colour(0xFF06B6D4));
        addPhase("Ambiance",   "Deep House",  110, 4, 90,  juce::Colour(0xFF3B82F6));
        addPhase("Montee",     "House",       120, 6, 60,  juce::Colour(0xFF10B981));
        addPhase("Fin",        "Ambient",     100, 3, 30,  juce::Colour(0xFF8B5CF6));
        break;
    case 2: // Festival
        addPhase("Opening",    "EDM",         125, 5, 30,  juce::Colour(0xFF3B82F6));
        addPhase("Main",       "House",       128, 7, 120, juce::Colour(0xFF10B981));
        addPhase("Headliner",  "Techno",      132, 10,90,  juce::Colour(0xFFEF4444));
        addPhase("Closing",    "Trance",      130, 6, 30,  juce::Colour(0xFF8B5CF6));
        break;
    case 3: // Wedding
        addPhase("Cocktail",   "Jazz",        100, 2, 60,  juce::Colour(0xFF06B6D4));
        addPhase("Diner",      "Soul",        90,  2, 120, juce::Colour(0xFF10B981));
        addPhase("Soiree",     "Pop",         120, 7, 180, juce::Colour(0xFFF59E0B));
        addPhase("After",      "Disco",       118, 5, 60,  juce::Colour(0xFFEF4444));
        break;
    case 4: // Corporate
        addPhase("Accueil",    "Ambient",     90,  2, 45,  juce::Colour(0xFF3B82F6));
        addPhase("Networking",  "Jazz",       100, 3, 60,  juce::Colour(0xFF06B6D4));
        addPhase("Soiree",     "Pop",         115, 6, 120, juce::Colour(0xFF10B981));
        addPhase("Fin",        "Chill",       95,  3, 30,  juce::Colour(0xFF8B5CF6));
        break;
    case 5: // Afterparty
        addPhase("Warm-up",    "Deep House",  120, 5, 30,  juce::Colour(0xFF3B82F6));
        addPhase("Peak",       "Techno",      130, 9, 120, juce::Colour(0xFFEF4444));
        addPhase("After-after","Ambient",     100, 3, 60,  juce::Colour(0xFF8B5CF6));
        break;
    case 6: // Birthday
        addPhase("Apero",      "Pop",         110, 3, 60,  juce::Colour(0xFF06B6D4));
        addPhase("Danse",      "EDM",         125, 8, 120, juce::Colour(0xFFF59E0B));
        addPhase("Slow / Fin", "R&B",         95,  4, 60,  juce::Colour(0xFF8B5CF6));
        break;
    default:
        addPhase("Accueil",    "Ambient",     95,  2, 60,  juce::Colour(0xFF06B6D4));
        addPhase("Cocktail",   "Jazz/Lounge", 105, 3, 90,  juce::Colour(0xFF3B82F6));
        addPhase("Diner",      "Soul/R&B",    100, 2, 120, juce::Colour(0xFF10B981));
        addPhase("Danse",      "House/Pop",   125, 8, 180, juce::Colour(0xFFF59E0B));
        addPhase("Fin",        "Chill",       95,  3, 30,  juce::Colour(0xFF8B5CF6));
        break;
    }

    m_timeline->phases = &m_soiree.phases;
}

void SoireePreparationView::onNewSoiree()
{
    m_soiree = Soiree();
    m_soiree.name = m_nameEditor->getText().isNotEmpty() ? m_nameEditor->getText() : "Nouvelle Soiree";
    m_soiree.venue = m_venueEditor->getText();
    m_soiree.date = m_dateEditor->getText();
    m_soiree.typeIndex = m_typeCombo->getSelectedId() - 1;
    m_soiree.totalDurationHours = m_durationSlider->getValue();

    m_checklist->items = {
        {"Sono / PA System OK",     &m_soiree.sonoOK},
        {"Lumieres / Lighting OK",  &m_soiree.lumieresOK},
        {"Rider technique OK",      &m_soiree.riderOK},
        {"Backup USB prete",        &m_soiree.backupUSB},
        {"Setlist imprimee",        &m_soiree.setlistPrinted},
        {"Casque + cable RCA",      &m_soiree.headphonesReady},
        {"Controleur MIDI/DJ pret", &m_soiree.controllerReady},
        {"Adaptateurs secteur",     &m_soiree.powerAdaptersReady},
        {"Contact organisateur OK", &m_soiree.contactOK},
        {"Paiement/contrat signe",  &m_soiree.paymentOK},
    };

    createDefaultPhases(m_soiree.typeIndex);
    m_selectedPhaseIndex = -1;
    rebuildTimeline();
    updatePhaseEditor();
    updatePhaseTrackList();
    m_checklist->repaint();
    repaint();
}

void SoireePreparationView::onLoadSoiree()
{
    m_fileChooser = std::make_unique<juce::FileChooser>(
        "Charger une soiree", juce::File{}, "*.json");
    m_fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (!file.existsAsFile()) return;

            auto text = file.loadFileAsString();
            try
            {
                auto j = nlohmann::json::parse(text.toStdString());
                m_soiree = Soiree();
                m_soiree.name = juce::String(std::string(j.value("name", "")));
                m_soiree.venue = juce::String(std::string(j.value("venue", "")));
                m_soiree.date = juce::String(std::string(j.value("date", "")));
                m_soiree.typeIndex = j.value("typeIndex", 0);
                m_soiree.totalDurationHours = j.value("totalDurationHours", 5.0);

                m_nameEditor->setText(m_soiree.name, false);
                m_venueEditor->setText(m_soiree.venue, false);
                m_dateEditor->setText(m_soiree.date, false);
                m_typeCombo->setSelectedId(m_soiree.typeIndex + 1, juce::dontSendNotification);
                m_durationSlider->setValue(m_soiree.totalDurationHours, juce::dontSendNotification);

                m_soiree.phases.clear();
                if (j.contains("phases"))
                {
                    for (auto& jp : j["phases"])
                    {
                        Phase p;
                        p.name = juce::String(std::string(jp.value("name", "")));
                        p.genre = juce::String(std::string(jp.value("genre", "")));
                        p.targetBPM = jp.value("targetBPM", 120.0f);
                        p.energyTarget = jp.value("energyTarget", 5);
                        p.durationMin = jp.value("durationMin", 30.0);
                        auto colHex = jp.value("color", "FF3B82F6");
                        p.color = juce::Colour((juce::uint32)std::stoul(colHex, nullptr, 16));
                        m_soiree.phases.push_back(p);
                    }
                }

                if (j.contains("checklist"))
                {
                    auto& cl = j["checklist"];
                    m_soiree.sonoOK = cl.value("sonoOK", false);
                    m_soiree.lumieresOK = cl.value("lumieresOK", false);
                    m_soiree.riderOK = cl.value("riderOK", false);
                    m_soiree.backupUSB = cl.value("backupUSB", false);
                    m_soiree.setlistPrinted = cl.value("setlistPrinted", false);
                }
                else
                {
                    loadChecklistFromJSON(m_soiree);
                }

                m_checklist->items = {
                    {"Sono / PA System OK",     &m_soiree.sonoOK},
                    {"Lumieres / Lighting OK",  &m_soiree.lumieresOK},
                    {"Rider technique OK",      &m_soiree.riderOK},
                    {"Backup USB prete",        &m_soiree.backupUSB},
                    {"Setlist imprimee",        &m_soiree.setlistPrinted},
                    {"Casque + cable RCA",      &m_soiree.headphonesReady},
                    {"Controleur MIDI/DJ pret", &m_soiree.controllerReady},
                    {"Adaptateurs secteur",     &m_soiree.powerAdaptersReady},
                    {"Contact organisateur OK", &m_soiree.contactOK},
                    {"Paiement/contrat signe",  &m_soiree.paymentOK},
                };

                m_timeline->phases = &m_soiree.phases;
                m_selectedPhaseIndex = -1;
                rebuildTimeline();
                updatePhaseEditor();
                updatePhaseTrackList();
                m_checklist->repaint();
                repaint();
            }
            catch (...) {}
        });
}

std::vector<Models::Track> SoireePreparationView::findMatchingTracks(int phaseIndex)
{
    if (!m_provider || phaseIndex < 0 || phaseIndex >= (int)m_soiree.phases.size())
        return {};

    auto& phase = m_soiree.phases[phaseIndex];

    float bpmMin = phase.targetBPM * 0.9f;
    float bpmMax = phase.targetBPM * 1.1f;
    float eMin = juce::jmax(0.0f, (float)phase.energyTarget - 2.0f);
    float eMax = juce::jmin(10.0f, (float)phase.energyTarget + 2.0f);

    auto tracks = m_provider->getTracksByFilter(
        bpmMin, bpmMax,
        "",  // any key
        phase.genre.toStdString(),
        eMin, eMax,
        0);  // any rating

    if (tracks.size() > 1)
    {
        std::vector<Models::Track> sorted;
        sorted.push_back(tracks[0]);
        std::vector<bool> used(tracks.size(), false);
        used[0] = true;

        for (size_t step = 1; step < tracks.size(); ++step)
        {
            auto& last = sorted.back();
            auto suggestions = m_scorer.suggestNext(last, tracks, 1);
            int bestIdx = -1;

            if (!suggestions.empty())
            {
                for (size_t j = 0; j < tracks.size(); ++j)
                {
                    if (!used[j] && tracks[j].id == suggestions[0].trackId)
                    { bestIdx = (int)j; break; }
                }
            }

            if (bestIdx < 0)
            {
                for (size_t j = 0; j < tracks.size(); ++j)
                    if (!used[j]) { bestIdx = (int)j; break; }
            }

            if (bestIdx >= 0) { sorted.push_back(tracks[bestIdx]); used[bestIdx] = true; }
        }
        tracks = sorted;
    }

    double totalSec = phase.durationMin * 60.0;
    double cumSec = 0;
    std::vector<Models::Track> result;
    for (auto& t : tracks)
    {
        if (cumSec >= totalSec) break;
        result.push_back(t);
        cumSec += t.duration;
    }

    return result;
}

void SoireePreparationView::onAutoFillAI()
{
    if (!m_provider)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            "Bibliotheque indisponible",
            "Aucune source de pistes n'est connectee - impossible de remplir automatiquement.");
        return;
    }
    if (m_soiree.phases.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            "Aucune phase",
            "Creez d'abord une soiree (bouton Nouveau) pour generer des phases.");
        return;
    }

    if (m_autoFillBusy->exchange(true))
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            juce::String::fromUTF8("Remplissage auto"),
            juce::String::fromUTF8("Un remplissage automatique est deja en cours. Patientez."),
            "OK");
        return;
    }

    auto progress = std::make_shared<juce::AlertWindow>(
        juce::String::fromUTF8("Remplissage automatique"),
        juce::String::fromUTF8("Recherche des pistes compatibles en arriere-plan. Veuillez patienter..."),
        juce::MessageBoxIconType::InfoIcon);
    progress->enterModalState(false, nullptr, false);
    progress->setVisible(true);

    const int phaseCount = (int)m_soiree.phases.size();
    auto matchedPerPhase = std::make_shared<std::vector<std::vector<Models::Track>>>(phaseCount);
    auto busy = m_autoFillBusy;
    juce::Component::SafePointer<SoireePreparationView> selfWeak(this);

    struct PhaseCriteria
    {
        float targetBPM = 120.0f;
        int energyTarget = 5;
        double durationMin = 30.0;
        std::string genre;
    };
    auto criteria = std::make_shared<std::vector<PhaseCriteria>>();
    criteria->reserve((size_t)phaseCount);
    for (int i = 0; i < phaseCount; ++i)
    {
        const auto& ph = m_soiree.phases[(size_t)i];
        PhaseCriteria c;
        c.targetBPM = ph.targetBPM;
        c.energyTarget = ph.energyTarget;
        c.durationMin = ph.durationMin;
        c.genre = ph.genre.toStdString();
        criteria->push_back(std::move(c));
    }
    auto* provider = m_provider;

    struct AutoFillJob : public juce::Thread
    {
        AutoFillJob() : juce::Thread("SoireeAutoFillJob") {}
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

    auto* job = new AutoFillJob();
    job->work = [provider, criteria, matchedPerPhase, phaseCount]() {
        Services::Preparation::SetCompatibilityScorer scorer;
        auto trackKey = [](const Models::Track& t) {
            auto title = juce::String(t.title).trim().toLowerCase();
            if (title.isEmpty()) return juce::String(t.id).toStdString();
            return (title + "|" + juce::String(t.artist).trim().toLowerCase()).toStdString();
        };
        std::unordered_set<std::string> usedKeys;
        for (int i = 0; i < phaseCount; ++i)
        {
            const auto& crit = (*criteria)[(size_t)i];

            float bpmMin = crit.targetBPM * 0.9f;
            float bpmMax = crit.targetBPM * 1.1f;
            float eMin = juce::jmax(0.0f, (float)crit.energyTarget - 2.0f);
            float eMax = juce::jmin(10.0f, (float)crit.energyTarget + 2.0f);

            auto tracks = provider->getTracksByFilter(
                bpmMin, bpmMax,
                "",
                crit.genre,
                eMin, eMax,
                0);

            {
                std::unordered_set<std::string> seen;
                std::vector<Models::Track> unique;
                for (auto& t : tracks)
                {
                    auto k = trackKey(t);
                    if (usedKeys.count(k) || !seen.insert(k).second) continue;
                    unique.push_back(t);
                }
                tracks = std::move(unique);
            }

            if (tracks.size() > 1)
            {
                std::vector<Models::Track> sorted;
                sorted.push_back(tracks[0]);
                std::vector<bool> used(tracks.size(), false);
                used[0] = true;

                for (size_t step = 1; step < tracks.size(); ++step)
                {
                    auto& last = sorted.back();
                    auto suggestions = scorer.suggestNext(last, tracks, 1);
                    int bestIdx = -1;

                    if (!suggestions.empty())
                    {
                        for (size_t j = 0; j < tracks.size(); ++j)
                        {
                            if (!used[j] && tracks[j].id == suggestions[0].trackId)
                            { bestIdx = (int)j; break; }
                        }
                    }

                    if (bestIdx < 0)
                    {
                        for (size_t j = 0; j < tracks.size(); ++j)
                            if (!used[j]) { bestIdx = (int)j; break; }
                    }

                    if (bestIdx >= 0) { sorted.push_back(tracks[bestIdx]); used[bestIdx] = true; }
                }
                tracks = sorted;
            }

            double totalSec = crit.durationMin * 60.0;
            double cumSec = 0;
            std::vector<Models::Track> result;
            for (auto& t : tracks)
            {
                if (cumSec >= totalSec) break;
                result.push_back(t);
                cumSec += t.duration;
            }

            for (auto& t : result) usedKeys.insert(trackKey(t));
            (*matchedPerPhase)[(size_t)i] = std::move(result);
        }
    };
    job->onDone = [selfWeak, matchedPerPhase, phaseCount, progress, busy]() {
        progress->exitModalState(0);
        progress->setVisible(false);
        busy->store(false);

        auto* self = selfWeak.getComponent();
        if (self == nullptr) return;
        if ((int)self->m_soiree.phases.size() != phaseCount) return;

        for (int i = 0; i < phaseCount; ++i)
        {
            auto& phase = self->m_soiree.phases[(size_t)i];
            std::unordered_set<int64_t> existingIds;
            for (auto& ex : phase.assignedTracks)
                existingIds.insert(ex.id);

            for (auto& t : (*matchedPerPhase)[(size_t)i])
            {
                if (existingIds.find(t.id) == existingIds.end())
                {
                    phase.assignedTracks.push_back(t);
                    existingIds.insert(t.id);
                }
            }
        }

        self->updatePhaseTrackList();
        if (self->m_timeline) self->m_timeline->repaint();
        self->repaint();
    };
    job->startThread();
}

static int computeCompatibilityScoreStatic(const Models::Track& a, const Models::Track& b)
{
    int score = 0;
    if (a.bpm > 0 && b.bpm > 0)
        score += juce::jmax(0, 40 - (int)(std::abs(a.bpm - b.bpm) * 4));
    juce::String ka = a.camelotKey.empty() ? juce::String(a.key) : juce::String(a.camelotKey);
    juce::String kb = b.camelotKey.empty() ? juce::String(b.key) : juce::String(b.camelotKey);
    if (areCamelotCompatible(ka, kb)) score += 30;
    float eDiff = std::abs(a.energy - b.energy);
    score += juce::jmax(0, 30 - (int)(eDiff * 10));
    return score;
}

static juce::String camelotCompatibleStatic(const juce::String& key)
{
    int idx = camelotIndex(key);
    if (idx < 0) return key;

    int num = (idx / 2) + 1;   // 1..12
    bool isMajor = (idx % 2 == 1); // B = major (odd index)

    juce::StringArray compatible;
    compatible.add(key.toUpperCase().trim());

    juce::String opposite = juce::String(num) + (isMajor ? "A" : "B");
    compatible.add(opposite);

    int plus1 = (num % 12) + 1;
    compatible.add(juce::String(plus1) + (isMajor ? "B" : "A"));

    int minus1 = ((num - 2 + 12) % 12) + 1;
    compatible.add(juce::String(minus1) + (isMajor ? "B" : "A"));

    return compatible.joinIntoString(", ");
}

void SoireePreparationView::onExportPDF()
{
    m_soiree.name = m_nameEditor->getText();
    m_soiree.venue = m_venueEditor->getText();
    m_soiree.date = m_dateEditor->getText();

    m_fileChooser = std::make_unique<juce::FileChooser>(
        juce::String::fromUTF8("Exporter la soirée en texte"), juce::File{}, "*.txt");
    m_fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{}) return;
            if (!file.hasFileExtension("txt")) file = file.withFileExtension("txt");

            juce::String out;
            out << "================================================\n";
            out << "  SOIREE : " << m_soiree.name << "\n";
            out << "  Lieu   : " << m_soiree.venue << "\n";
            out << "  Date   : " << m_soiree.date << "\n";
            out << "  Type   : " << m_typeCombo->getText() << "\n";
            out << "  Duree  : " << juce::String(m_soiree.totalDurationHours, 1) << "h\n";
            out << "================================================\n\n";

            double cumMin = 0;
            for (int i = 0; i < (int)m_soiree.phases.size(); ++i)
            {
                auto& p = m_soiree.phases[i];
                int h = (int)(cumMin / 60), m = (int)cumMin % 60;
                juce::String start = juce::String(h) + "h" + (m < 10 ? "0" : "") + juce::String(m);
                cumMin += p.durationMin;
                int h2 = (int)(cumMin / 60), m2 = (int)cumMin % 60;
                juce::String end = juce::String(h2) + "h" + (m2 < 10 ? "0" : "") + juce::String(m2);

                out << "--- Phase " << (i + 1) << ": " << p.name << " ---\n";
                out << "  Horaire : " << start << " -> " << end << " (" << (int)p.durationMin << " min)\n";
                out << "  Genre   : " << p.genre << "\n";
                out << "  BPM     : " << juce::String(p.targetBPM, 0) << "\n";
                out << "  Energie : " << p.energyTarget << "/10\n";
                out << "  Pistes  : " << (int)p.assignedTracks.size() << "\n";

                for (int j = 0; j < (int)p.assignedTracks.size(); ++j)
                {
                    auto& t = p.assignedTracks[j];
                    int tmin = (int)(t.duration / 60.0);
                    int tsec = (int)t.duration % 60;
                    out << "    " << (j + 1) << ". " << juce::String(t.artist)
                        << " - " << juce::String(t.title)
                        << "  [" << juce::String(t.bpm, 1) << " BPM, "
                        << juce::String(t.camelotKey.empty() ? t.key : t.camelotKey)
                        << ", E:" << juce::String(t.energy, 0)
                        << ", " << tmin << ":" << (tsec < 10 ? "0" : "") << tsec
                        << "]\n";
                }
                out << "\n";
            }

            out << "--- CHECKLIST ---\n";
            out << "  [" << (m_soiree.sonoOK ? "X" : " ") << "] Sono / PA System\n";
            out << "  [" << (m_soiree.lumieresOK ? "X" : " ") << "] Lumieres / Lighting\n";
            out << "  [" << (m_soiree.riderOK ? "X" : " ") << "] Rider technique\n";
            out << "  [" << (m_soiree.backupUSB ? "X" : " ") << "] Backup USB\n";
            out << "  [" << (m_soiree.setlistPrinted ? "X" : " ") << "] Setlist imprimee\n";

            file.replaceWithText(out);
            m_listeners.call(&Listener::soireeExportRequested, juce::String("PDF"));
        });
}

void SoireePreparationView::onExportJSON()
{
    m_soiree.name = m_nameEditor->getText();
    m_soiree.venue = m_venueEditor->getText();
    m_soiree.date = m_dateEditor->getText();

    auto defaultDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("BeatMate").getChildFile("events");
    defaultDir.createDirectory();
    const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
    juce::String safeName = m_soiree.name.isNotEmpty()
        ? m_soiree.name.replaceCharacters(" /\\", "___")
        : juce::String("soiree_") + stamp;
    auto defaultFile = defaultDir.getChildFile(safeName + ".bmevent");

    m_fileChooser = std::make_unique<juce::FileChooser>(
        juce::String::fromUTF8("Exporter la soirée (.bmevent / .json)"),
        defaultFile, "*.bmevent;*.json");
    m_fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{}) return;

            nlohmann::json j;
            j["schema"]           = "bmevent/1.0";
            j["beatMateVersion"]  = BEATMATE_VERSION;
            j["exportedAt"]       = juce::Time::getCurrentTime().toISO8601(true).toStdString();
            j["name"] = m_soiree.name.toStdString();
            j["venue"] = m_soiree.venue.toStdString();
            j["date"] = m_soiree.date.toStdString();
            j["typeIndex"] = m_soiree.typeIndex;
            j["totalDurationHours"] = m_soiree.totalDurationHours;

            j["phases"] = nlohmann::json::array();
            for (auto& p : m_soiree.phases)
            {
                nlohmann::json jp;
                jp["name"] = p.name.toStdString();
                jp["genre"] = p.genre.toStdString();
                jp["targetBPM"] = p.targetBPM;
                jp["energyTarget"] = p.energyTarget;
                jp["durationMin"] = p.durationMin;
                jp["color"] = p.color.toString().toStdString();

                jp["tracks"] = nlohmann::json::array();
                for (auto& t : p.assignedTracks)
                {
                    nlohmann::json jt;
                    jt["id"] = t.id;
                    jt["title"] = t.title;
                    jt["artist"] = t.artist;
                    jt["bpm"] = t.bpm;
                    jt["key"] = t.key;
                    jt["camelotKey"] = t.camelotKey;
                    jt["energy"] = t.energy;
                    jt["duration"] = t.duration;
                    jt["filePath"] = t.filePath;
                    jp["tracks"].push_back(jt);
                }
                j["phases"].push_back(jp);
            }

            j["checklist"] = {
                {"sonoOK",            m_soiree.sonoOK},
                {"lumieresOK",        m_soiree.lumieresOK},
                {"riderOK",           m_soiree.riderOK},
                {"backupUSB",         m_soiree.backupUSB},
                {"setlistPrinted",    m_soiree.setlistPrinted},
                {"headphonesReady",   m_soiree.headphonesReady},
                {"controllerReady",   m_soiree.controllerReady},
                {"powerAdaptersReady", m_soiree.powerAdaptersReady},
                {"contactOK",         m_soiree.contactOK},
                {"paymentOK",         m_soiree.paymentOK}
            };

            file.replaceWithText(juce::String(j.dump(2)));
            m_listeners.call(&Listener::soireeExportRequested, juce::String("BMEVENT"));
        });
}

void SoireePreparationView::onExportM3U()
{
    m_fileChooser = std::make_unique<juce::FileChooser>(
        "Exporter les playlists M3U", juce::File{}, "*.m3u");
    m_fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto baseFile = fc.getResult();
            if (baseFile == juce::File{}) return;

            auto dir = baseFile.getParentDirectory();
            auto baseName = baseFile.getFileNameWithoutExtension();

            for (int i = 0; i < (int)m_soiree.phases.size(); ++i)
            {
                auto& phase = m_soiree.phases[i];
                juce::String safeName = phase.name.replaceCharacters(" /\\", "___");
                auto phaseFile = dir.getChildFile(baseName + "_" + juce::String(i + 1) + "_" + safeName + ".m3u");

                juce::String content;
                content << "#EXTM3U\n";
                content << "#PLAYLIST:" << m_soiree.name << " - " << phase.name << "\n";
                for (auto& t : phase.assignedTracks)
                {
                    int dur = (int)t.duration;
                    content << "#EXTINF:" << dur << "," << juce::String(t.artist) << " - " << juce::String(t.title) << "\n";
                    content << juce::String(t.filePath) << "\n";
                }
                phaseFile.replaceWithText(content);
            }
            m_listeners.call(&Listener::soireeExportRequested, juce::String("M3U"));
        });
}

void SoireePreparationView::selectPhase(int phaseIndex)
{
    m_selectedPhaseIndex = phaseIndex;
    m_timeline->selectedPhase = phaseIndex;
    updatePhaseEditor();
    updatePhaseTrackList();
    m_timeline->repaint();

    bool phaseSel = (phaseIndex >= 0 && phaseIndex < (int)m_soiree.phases.size());
    if (m_removePhaseBtn) m_removePhaseBtn->setEnabled(phaseSel && m_soiree.phases.size() > 1);
    if (m_addTrackBtn)    m_addTrackBtn->setEnabled(phaseSel);
}

void SoireePreparationView::updatePhaseEditor()
{
    if (m_selectedPhaseIndex >= 0 && m_selectedPhaseIndex < (int)m_soiree.phases.size())
    {
        auto& p = m_soiree.phases[m_selectedPhaseIndex];
        m_phaseNameEditor->setText(p.name, false);

        bool found = false;
        for (int i = 1; i <= m_phaseGenreCombo->getNumItems(); ++i)
        {
            if (m_phaseGenreCombo->getItemText(i - 1).equalsIgnoreCase(p.genre))
            {
                m_phaseGenreCombo->setSelectedId(i, juce::dontSendNotification);
                found = true;
                break;
            }
        }
        if (!found)
            m_phaseGenreCombo->setText(p.genre, juce::dontSendNotification);

        m_phaseBpmSlider->setValue(p.targetBPM, juce::dontSendNotification);
        m_phaseEnergySlider->setValue(p.energyTarget, juce::dontSendNotification);
        m_phaseDurSlider->setValue(p.durationMin, juce::dontSendNotification);
    }
}

void SoireePreparationView::updatePhaseTrackList()
{
    bool hasTracks = false;
    if (m_selectedPhaseIndex >= 0 && m_selectedPhaseIndex < (int)m_soiree.phases.size())
    {
        m_phaseTrackModel->tracks = &m_soiree.phases[m_selectedPhaseIndex].assignedTracks;
        hasTracks = !m_soiree.phases[m_selectedPhaseIndex].assignedTracks.empty();
    }
    else
        m_phaseTrackModel->tracks = nullptr;

    if (m_removeTrackBtn) m_removeTrackBtn->setEnabled(hasTracks);

    m_phaseTrackList->updateContent();
    m_phaseTrackList->repaint();
}

void SoireePreparationView::syncEditorToPhase()
{
    if (m_selectedPhaseIndex < 0 || m_selectedPhaseIndex >= (int)m_soiree.phases.size()) return;
    auto& p = m_soiree.phases[m_selectedPhaseIndex];
    p.name = m_phaseNameEditor->getText();
    p.genre = m_phaseGenreCombo->getText();
    p.targetBPM = (float)m_phaseBpmSlider->getValue();
    p.energyTarget = (int)m_phaseEnergySlider->getValue();
    p.durationMin = m_phaseDurSlider->getValue();
    m_timeline->repaint();
}

void SoireePreparationView::applyEnergyProfile(int profileIndex)
{
    if (profileIndex < 0 || profileIndex >= (int)m_presets.size()) return;
    auto& profile = m_presets[profileIndex];
    auto& phases = m_soiree.phases;
    if (phases.empty()) return;

    int n = (int)phases.size();
    int m = (int)profile.energyLevels.size();
    for (int i = 0; i < n; ++i)
    {
        float t = (n == 1) ? 0.0f : (float)i / (float)(n - 1);
        float fIdx = t * (m - 1);
        int lo = juce::jmax(0, (int)fIdx);
        int hi = juce::jmin(m - 1, lo + 1);
        float frac = fIdx - lo;
        int energy = juce::jlimit(1, 10, (int)std::round(profile.energyLevels[lo] * (1.0f - frac) + profile.energyLevels[hi] * frac));
        phases[i].energyTarget = energy;
    }

    updatePhaseEditor();
    m_timeline->repaint();
}

void SoireePreparationView::rebuildTimeline()
{
    m_timeline->phases = &m_soiree.phases;
    m_timeline->repaint();
}

void SoireePreparationView::paint(juce::Graphics& g)
{
    int W = getWidth();
    int H = getHeight();
    int margin = 20;
    int headerH = 56;

    ProDraw::viewBackground(g, W, H);

    {
        juce::Rectangle<float> headerRect(0.0f, 0.0f, (float)W, (float)headerH);
        g.setColour(Colors::bgDarkest().withAlpha(0.9f));
        g.fillRect(headerRect);

        g.setFont(juce::Font("Segoe UI", 22.0f, juce::Font::bold));
        g.setColour(Colors::textPrimary());
        g.drawText(juce::String::fromUTF8("PREPARATION EVENEMENT"), margin, 6, 360, 28, juce::Justification::centredLeft);

        juce::String subtitle;
        auto appendPart = [&subtitle](const juce::String& part)
        {
            if (part.isEmpty()) return;
            if (subtitle.isNotEmpty()) subtitle += juce::String::fromUTF8("   |   ");
            subtitle += part;
        };
        if (m_nameEditor)  appendPart(m_nameEditor->getText());
        if (m_venueEditor) appendPart(m_venueEditor->getText());
        if (m_dateEditor)  appendPart(m_dateEditor->getText());
        appendPart(juce::String(m_soiree.totalDurationHours, 1) + "h");
        if (subtitle.isEmpty())
            subtitle = juce::String::fromUTF8("Aucun evenement - cliquez 'Nouveau' pour commencer");

        g.setFont(juce::Font("Segoe UI", 12.0f, juce::Font::plain));
        g.setColour(Colors::textMuted());
        g.drawText(subtitle, margin + 360, 18, juce::jmax(120, W - margin - 360 - 230), 22,
                   juce::Justification::centredLeft);

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

    ProDraw::vignette(g, (float)W, (float)H, 10.0f);
}

namespace {
class SoireeAddTracksDialogContent : public juce::Component
{
public:
    SoireeAddTracksDialogContent(LibraryBrowserPanel* browser,
                                  std::function<void(const std::vector<Models::Track>&)> onAdd)
        : m_browser(browser), m_onAdd(std::move(onAdd))
    {
        if (m_browser) addAndMakeVisible(*m_browser);

        m_hint = std::make_unique<juce::Label>("hint",
            "Selectionne une ou plusieurs pistes puis clique 'Ajouter' (double-clic fonctionne aussi).");
        m_hint->setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.75f));
        m_hint->setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
        addAndMakeVisible(*m_hint);

        m_addBtn = std::make_unique<juce::TextButton>(BM_TJ("common.add"));
        m_addBtn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF22C55E));
        m_addBtn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        m_addBtn->onClick = [this] {
            if (!m_browser) return;
            auto sel = m_browser->getSelectedTracks();
            if (sel.empty()) {
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::InfoIcon)
                        .withTitle("Aucune piste selectionnee")
                        .withMessage("Selectionne au moins une piste dans la liste avant d'ajouter.")
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

        m_closeBtn = std::make_unique<juce::TextButton>(BM_TJ("common.close"));
        m_closeBtn->onClick = [this] {
            if (auto* top = getTopLevelComponent())
                if (auto* dw = dynamic_cast<juce::DialogWindow*>(top))
                    dw->exitModalState(0);
        };
        addAndMakeVisible(*m_closeBtn);

        setSize(780, 600);
    }

    ~SoireeAddTracksDialogContent() override {
        if (m_browser) removeChildComponent(m_browser); // don't delete — non-owning
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

void SoireePreparationView::onBrowseLibrary()
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

    auto* content = new SoireeAddTracksDialogContent(
        m_libraryBrowser.get(),
        [this](const std::vector<Models::Track>& tracks) {
            if (m_libListener) {
                if (auto* l = dynamic_cast<LibraryBrowserPanel::Listener*>(m_libListener.get()))
                    l->tracksDropped(tracks);
            }
        });

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(content);
    opts.dialogTitle                   = "Ajouter des pistes a la soiree";
    opts.dialogBackgroundColour        = juce::Colour(0xFF0E1117);
    opts.componentToCentreAround       = this;
    opts.escapeKeyTriggersCloseButton  = true;
    opts.useNativeTitleBar             = true;
    opts.resizable                     = true;
    opts.useBottomRightCornerResizer   = false;
    opts.launchAsync();
}

void SoireePreparationView::refreshValidation()
{
    std::vector<Models::Track> allTracks;
    for (auto& phase : m_soiree.phases)
        for (auto& t : phase.assignedTracks)
            allTracks.push_back(t);

    if (m_timeline) m_timeline->repaint();
    updatePhaseTrackList();
    repaint();
}

void SoireePreparationView::onStorylineArc(int arcType)
{
    if (!m_provider) return;
    auto pool = m_provider->getAllTracks();
    double totalMin = m_soiree.totalDurationHours * 60.0;

    Services::Preparation::StoryArc arc;
    switch (arcType)
    {
        case 0: arc = m_storylinePlanner.planClassicArc(pool, totalMin); break;
        case 1: arc = m_storylinePlanner.planDoubleDropArc(pool, totalMin); break;
        case 2: arc = m_storylinePlanner.planMarathonArc(pool, totalMin); break;
        default: arc = m_storylinePlanner.planClassicArc(pool, totalMin); break;
    }

    m_soiree.phases.clear();
    juce::Colour phaseColors[] = {
        Colors::primary(), Colors::accent(), Colors::error(),
        Colors::warning(), Colors::success()
    };

    for (int i = 0; i < (int)arc.chapters.size(); ++i)
    {
        auto& ch = arc.chapters[i];
        Phase p;
        p.name = juce::String(ch.name);
        p.genre = juce::String(ch.mood);
        p.targetBPM = (ch.targetEnergyStart + ch.targetEnergyEnd) * 6.0f + 80.0f;
        p.energyTarget = (int)((ch.targetEnergyStart + ch.targetEnergyEnd) * 0.5f);
        p.durationMin = ch.durationMinutes;
        p.color = phaseColors[i % 5];
        p.assignedTracks = ch.tracks;
        m_soiree.phases.push_back(p);
    }

    m_selectedPhaseIndex = 0;
    rebuildTimeline();
    updatePhaseEditor();
    updatePhaseTrackList();
    repaint();
}

void SoireePreparationView::resized()
{
    int W = getWidth();
    int H = getHeight();
    int margin = 20;
    int headerH = 56;
    int bottomH = 52;
    int gap = 8;
    int panelPad = 14; // inner padding within glass panels
    int lw = 58;       // label width for form fields

    m_titleLabel->setBounds(margin, 10, 300, 30);
    m_newBtn->setBounds(W - margin - 210, 12, 96, 30);
    m_mySoireesBtn->setBounds(W - margin - 108, 12, 104, 30);

    m_viewport->setBounds(0, headerH, W, H - headerH);

    int contentTop = 6;  // small gap at top of content area
    int contentH = H - headerH - bottomH - 14;
    int leftW = juce::jmax(240, W / 4);
    int rightW = juce::jmax(240, W / 4);
    int centerW = W - leftW - rightW - margin * 2 - gap * 2;
    int centerX = margin + leftW + gap;
    int rightX = W - rightW - margin;

    int lx = margin + panelPad;
    int lInnerW = leftW - panelPad * 2;
    int ly = contentTop + 30; // after section label

    m_nameLabel->setBounds(lx, ly, lw, 22);
    m_nameEditor->setBounds(lx + lw, ly, lInnerW - lw, 24);
    ly += 28;

    m_venueLabel->setBounds(lx, ly, lw, 22);
    m_venueEditor->setBounds(lx + lw, ly, lInnerW - lw, 24);
    ly += 28;

    m_dateLabel->setBounds(lx, ly, lw, 22);
    m_dateEditor->setBounds(lx + lw, ly, lInnerW - lw, 24);
    ly += 28;

    m_typeLabel->setBounds(lx, ly, lw, 22);
    m_typeCombo->setBounds(lx + lw, ly, lInnerW - lw, 24);
    ly += 28;

    m_durationLabel->setBounds(lx, ly, lw, 22);
    m_durationSlider->setBounds(lx + lw, ly, lInnerW - lw, 24);
    ly += 32;

    ly = contentTop + 178;
    m_profileLabel->setBounds(lx, ly, 80, 22);
    m_profileCombo->setBounds(lx + 82, ly, lInnerW - 82, 24);
    ly += 30;

    ly = contentTop + 218;
    int timelineH = juce::jmax(100, contentH - (ly - contentTop) - 50);
    m_timeline->setBounds(lx, ly, lInnerW, timelineH);

    int btnY = ly + timelineH + 4;
    int btnW = (lInnerW - 6) / 2;
    m_addPhaseBtn->setBounds(lx, btnY, btnW, 24);
    m_removePhaseBtn->setBounds(lx + btnW + 6, btnY, btnW, 24);

    int cx = centerX + panelPad;
    int cInnerW = centerW - panelPad * 2;
    int cy = contentTop + 28; // after section label

    int edFieldW = (cInnerW - lw * 2 - 10) / 2;
    if (edFieldW < 80) edFieldW = cInnerW - lw; // fallback for narrow

    if (cInnerW > 400) // wide enough for 2-column layout
    {
        m_phaseNameLabel->setBounds(cx, cy, lw, 22);
        m_phaseNameEditor->setBounds(cx + lw, cy, edFieldW, 24);
        m_phaseGenreLabel->setBounds(cx + lw + edFieldW + 10, cy, lw, 22);
        m_phaseGenreCombo->setBounds(cx + lw * 2 + edFieldW + 10, cy, edFieldW, 24);
        cy += 28;

        m_phaseBpmLabel->setBounds(cx, cy, lw, 22);
        m_phaseBpmSlider->setBounds(cx + lw, cy, edFieldW, 24);
        m_phaseEnergyLabel->setBounds(cx + lw + edFieldW + 10, cy, lw, 22);
        m_phaseEnergySlider->setBounds(cx + lw * 2 + edFieldW + 10, cy, edFieldW, 24);
        cy += 28;

        m_phaseDurLabel->setBounds(cx, cy, lw, 22);
        m_phaseDurSlider->setBounds(cx + lw, cy, edFieldW, 24);
        m_phaseColorLabel->setBounds(cx + lw + edFieldW + 10, cy, lw, 22);
        m_phaseColorBtn->setBounds(cx + lw * 2 + edFieldW + 10, cy, edFieldW, 24);
        cy += 28;
    }
    else // narrow: single column
    {
        m_phaseNameLabel->setBounds(cx, cy, lw, 22);
        m_phaseNameEditor->setBounds(cx + lw, cy, cInnerW - lw, 24);
        cy += 32;
        m_phaseGenreLabel->setBounds(cx, cy, lw, 22);
        m_phaseGenreCombo->setBounds(cx + lw, cy, cInnerW - lw, 24);
        cy += 32;
        m_phaseBpmLabel->setBounds(cx, cy, lw, 22);
        m_phaseBpmSlider->setBounds(cx + lw, cy, cInnerW - lw, 24);
        cy += 32;
        m_phaseEnergyLabel->setBounds(cx, cy, lw, 22);
        m_phaseEnergySlider->setBounds(cx + lw, cy, cInnerW - lw, 24);
        cy += 32;
        m_phaseDurLabel->setBounds(cx, cy, lw, 22);
        m_phaseDurSlider->setBounds(cx + lw, cy, cInnerW - lw, 24);
        cy += 32;
        m_phaseColorLabel->setBounds(cx, cy, lw, 22);
        m_phaseColorBtn->setBounds(cx + lw, cy, cInnerW - lw, 24);
        cy += 28;
    }

    m_autoFillBtn->setBounds(cx, cy, juce::jmin(cInnerW, 200), 30);
    cy += 38;

    int trackListH = contentH - (cy - contentTop) - 40;
    if (trackListH < 120) trackListH = 120;
    m_phaseTrackList->setBounds(cx, cy, cInnerW, trackListH);
    cy += trackListH + 4;

    m_addTrackBtn->setBounds(cx, cy, 130, 26);
    m_removeTrackBtn->setBounds(cx + 138, cy, 150, 26);
    m_clearTracksBtn->setBounds(cx + 296, cy, juce::jmax(80, juce::jmin(110, cInnerW - 296)), 26);

    int rx = rightX + panelPad;
    int rInnerW = rightW - panelPad * 2;
    int ry = contentTop + 28 + 24; // after section label + CHECKLIST sub-label

    int checklistH = 24 + 10 * 32 + 8;
    m_checklist->setBounds(rx, ry, rInnerW, checklistH);
    ry += checklistH + 16;

    int exportBtnW = juce::jmin(rInnerW, 180);
    m_exportPDFBtn->setBounds(rx, ry, exportBtnW, 32);
    ry += 38;
    m_exportJSONBtn->setBounds(rx, ry, exportBtnW, 32);
    ry += 38;
    m_exportM3UBtn->setBounds(rx, ry, exportBtnW, 32);

    if (m_browseLibraryBtn)
        m_browseLibraryBtn->setBounds(cx + cInnerW - 220, cy - 34, 220, 26);

    if (m_phaseTimeline && m_phaseTimeline->isVisible())
    {
        int tW = juce::jmin(centerW + rightW + gap, W - 2 * margin);
        int tH = juce::jmin(280, H - headerH - bottomH - 40);
        m_phaseTimeline->setBounds((W - tW) / 2, (H - tH) / 2, tW, tH);
        m_phaseTimeline->toFront(true);
    }
    if (m_handoverList && m_handoverList->isVisible())
    {
        int hW = juce::jmin(480, W - 2 * margin);
        int hH = juce::jmin(360, H - headerH - bottomH - 40);
        m_handoverList->setBounds((W - hW) / 2, (H - hH) / 2, hW, hH);
        m_handoverList->toFront(true);
    }

    int contentBottom = juce::jmax(contentTop + contentH + bottomH + 20, H);
    m_content->setSize(W, contentBottom);
}

} // namespace BeatMate::UI
