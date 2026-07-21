#include "PlaylistPreparationView.h"
#include "../styles/ColorPalette.h"
#include "analysis/AnalysisColumns.h"
#include "../widgets/browser/LibraryBrowserPanel.h"
#include "../../services/library/TrackDataProvider.h"
#include "../../services/config/I18n.h"
#include "../utils/ViewPrefs.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <spdlog/spdlog.h>
#include <set>
#include <map>
#include <fstream>

namespace BeatMate::UI {

namespace {

juce::String genreFamily(const juce::String& rawIn)
{
    const juce::String g = rawIn.trim().toLowerCase();
    if (g.isEmpty()) return {};

    struct Fam { const char* name; std::vector<const char*> keys; };
    static const std::vector<Fam> families = {
        { "Zouk",                { "zouk" } },
        { "Kompa / Compas",      { "kompa", "compas", "konpa" } },
        { "Kizomba",             { "kizomba", "semba" } },
        { "Reggae / Dancehall",  { "reggae", "reggea", "dancehall", "ragga", "ska", "dub" } },
        { "Afro",                { "afro", "coupe", "coup\xc3\xa9", "ndombolo", "makossa" } },
        { "Salsa / Latino",      { "salsa", "latin", "bachata", "merengue", "reggaeton", "cumbia", "kuduro" } },
        { "House",               { "house" } },
        { "Techno",              { "techno" } },
        { "Trance",              { "trance" } },
        { "Disco / Funk",        { "disco", "funk", "boogie" } },
        { "Soul / R&B",          { "soul", "motown", "r&b", "rnb", "r'n'b", "rhythm and blues" } },
        { "Hip-Hop / Rap",       { "hip", "rap", "trap", "drill" } },
        { "M\xc3\xa9tal",        { "metal", "m\xc3\xa9tal" } },
        { "Rock",                { "rock", "punk", "grunge" } },
        { "\xc3\x89lectro / Dance", { "electro", "\xc3\xa9lectro", "edm", "eurodance", "dancefloor", "dance", "club" } },
        { "Slow / Ballade",      { "slow", "ballad", "balade", "love" } },
        { "Vari\xc3\xa9t\xc3\xa9 fran\xc3\xa7\x61ise", { "vari\xc3\xa9t", "variet", "chanson", "fran\xc3\xa7", "francais" } },
        { "Pop",                 { "pop" } },
        { "Jazz",                { "jazz", "swing" } },
        { "Blues",               { "blues" } },
        { "Country",             { "country" } },
        { "Classique",           { "classique", "classical", "orchestr" } },
        { "Gospel",              { "gospel" } },
        { "Th\xc3\xa8me / B.O.", { "theme", "th\xc3\xa8me", "bande originale", "soundtrack", "g\xc3\xa9n\xc3\xa9rique", "generique" } },
    };

    for (const auto& f : families)
        for (const char* k : f.keys)
            if (g.contains(juce::String::fromUTF8(k)))
                return juce::String::fromUTF8(f.name);

    return rawIn.trim();
}

} // namespace

int PlaylistPreparationView::camelotNumber(const juce::String& key) const
{
    juce::String k = key.trim().toUpperCase();
    if (k.isEmpty()) return -1;

    if (k.containsChar('A') || k.containsChar('B'))
    {
        juce::String num = k.upToFirstOccurrenceOf("A", false, true);
        if (num.isEmpty()) num = k.upToFirstOccurrenceOf("B", false, true);
        int n = num.getIntValue();
        if (n >= 1 && n <= 12) return n;
    }
    return -1;
}

bool PlaylistPreparationView::camelotLetter(const juce::String& key) const
{
    if (key.isEmpty()) return false;
    auto lastChar = key[key.length() - 1];
    if (lastChar == 'B' || lastChar == 'b') return true;  // major
    if (lastChar == 'A' || lastChar == 'a') return false; // minor
    if (key.endsWithIgnoreCase("m")) return false;
    return true; // default to major for standard note names (C, D, E...)
}

bool PlaylistPreparationView::areKeysCompatible(const juce::String& k1, const juce::String& k2) const
{
    int n1 = camelotNumber(k1), n2 = camelotNumber(k2);
    if (n1 < 0 || n2 < 0) return true; // unknown = compatible
    bool l1 = camelotLetter(k1), l2 = camelotLetter(k2);

    if (n1 == n2 && l1 == l2) return true;
    int diff = std::abs(n1 - n2);
    if (diff == 1 && l1 == l2) return true;
    if (diff == 11 && l1 == l2) return true; // 12->1 wrap
    if (n1 == n2 && l1 != l2) return true;
    return false;
}

float PlaylistPreparationView::computeTrackCompatibility(int idxA, int idxB) const
{
    const std::vector<TrackInfo>* tracks = nullptr;
    if (m_isFilterActive)
        tracks = &m_filteredTracks;
    else if (m_currentPlaylistIndex >= 0 && m_currentPlaylistIndex < (int)m_playlists.size())
        tracks = &m_playlists[m_currentPlaylistIndex].tracks;
    if (!tracks || idxA < 0 || idxB < 0 || idxA >= (int)tracks->size() || idxB >= (int)tracks->size())
        return 0.0f;

    const auto& a = (*tracks)[idxA];
    const auto& b = (*tracks)[idxB];

    Models::Track ta = toModelTrack(a);
    Models::Track tb = toModelTrack(b);
    auto res = m_scorer.score(ta, tb);
    return juce::jlimit(0.0f, 100.0f, static_cast<float>(res.score));
}

juce::String PlaylistPreparationView::getSuggestionText() const
{
    if (m_currentPlaylistIndex < 0 || m_currentPlaylistIndex >= (int)m_playlists.size())
        return "";
    auto& tracks = m_playlists[m_currentPlaylistIndex].tracks;
    if (tracks.size() < 2) return "Ajoutez plus de pistes pour des suggestions.";

    float worstScore = 100.0f;
    int worstIdx = -1;
    for (int i = 0; i < (int)tracks.size() - 1; ++i)
    {
        float s = computeTrackCompatibility(i, i + 1);
        if (s < worstScore)
        {
            worstScore = s;
            worstIdx = i;
        }
    }

    if (worstIdx >= 0 && worstScore < 60.0f)
    {
        float idealBpm = (tracks[worstIdx].bpm + tracks[worstIdx + 1].bpm) * 0.5f;
        return "Ajouter un track a " + juce::String((int)idealBpm) +
               " BPM entre les pistes " + juce::String(worstIdx + 1) +
               " et " + juce::String(worstIdx + 2);
    }
    return "Playlist bien equilibree !";
}

void PlaylistPreparationView::PlaylistListModel::paintListBoxItem(
    int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= playlistNames.size()) return;

    if (selected)
    {
        juce::ColourGradient grad(Colors::primary().withAlpha(0.3f), 0.0f, 0.0f,
                                  Colors::primary().withAlpha(0.1f), (float)w, 0.0f, false);
        g.setGradientFill(grad);
        g.fillRect(0, 0, w, h);
        g.setColour(Colors::primary());
        g.fillRect(0, 0, 3, h);
    }

    bool isAllTracks = playlistNames[row].startsWith("Toutes");
    bool isSmart = (row < (int)isSmartFlags.size()) && isSmartFlags[row];
    juce::Colour iconCol = selected ? Colors::primary()
                                    : (isSmart ? Colors::accent() : Colors::textDim());
    g.setColour(iconCol.withAlpha(0.18f));
    g.fillEllipse(4.0f, (h - 20.0f) / 2.0f, 20.0f, 20.0f);
    g.setColour(iconCol);
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    const char* glyph = isAllTracks ? ">" : (isSmart ? "S" : "#");
    g.drawText(glyph, 4, 0, 20, h, juce::Justification::centred);

    g.setColour(selected ? Colors::textPrimary() : Colors::textSecondary());
    g.setFont(juce::Font(13.0f, juce::Font::bold));
    g.drawText(playlistNames[row], 28, 0, w - 32, h / 2, juce::Justification::bottomLeft);

    juce::String name = playlistNames[row];
    juce::String countStr;
    if (name.contains("(") && name.contains(")"))
    {
        countStr = name.fromFirstOccurrenceOf("(", false, false).upToFirstOccurrenceOf(")", false, false);
    }

    if (countStr.isNotEmpty())
    {
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        int badgeW = 56;
        int badgeX = 28;
        int badgeY = h / 2 + 2;
        g.setColour(Colors::accent().withAlpha(0.2f));
        g.fillRoundedRectangle((float)badgeX, (float)badgeY, (float)badgeW, 14.0f, 4.0f);
        g.setColour(Colors::accent());
        g.drawText(countStr + " pistes", badgeX, badgeY, badgeW, 14, juce::Justification::centred);
    }
    else
    {
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.setColour(Colors::textDim());
        g.drawText("playlist", 28, h / 2 + 2, 50, 14, juce::Justification::centredLeft);
    }

    g.setColour(Colors::border().withAlpha(0.3f));
    g.drawHorizontalLine(h - 1, 0.0f, (float)w);
}

void PlaylistPreparationView::TrackListModel::paintListBoxItem(
    int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (!tracks || row < 0 || row >= (int)tracks->size()) return;
    const auto& t = (*tracks)[row];

    const juce::Colour bpmBlue = Colors::bpmBadge();
    const int camelotNum = t.key.initialSectionContainingOnly("0123456789").getIntValue();
    const bool looksCamelot = camelotNum >= 1 && camelotNum <= 12
        && (t.key.endsWithIgnoreCase("A") || t.key.endsWithIgnoreCase("B"));
    const juce::Colour keyViolet = looksCamelot
        ? AnalysisColumns::camelotColour(t.key)
        : Colors::keyBadge();

    if (selected)
    {
        juce::ColourGradient selGrad(Colors::primary().withAlpha(0.28f), 0.0f, 0.0f,
                                      Colors::primary().withAlpha(0.08f), (float)w, 0.0f, false);
        g.setGradientFill(selGrad);
        g.fillRect(0, 0, w, h);
        g.setColour(Colors::primary());
        g.fillRect(0, 0, 3, h);
    }
    else if (row % 2 == 0)
    {
        g.setColour(Colors::bgDark().withAlpha(0.5f));
        g.fillRect(0, 0, w, h);
    }

    int x = 4;

    {
        juce::Colour gripCol = selected ? Colors::textSecondary() : Colors::textDim().withAlpha(0.6f);
        g.setColour(gripCol);
        for (int col = 0; col < 2; ++col)
        {
            for (int li = 0; li < 3; ++li)
            {
                float dotX = (float)(x + col * 5);
                float dotY = (float)(h / 2 - 5 + li * 5);
                g.fillRoundedRectangle(dotX, dotY, 3.0f, 3.0f, 1.5f);
            }
        }
    }
    x += 16;

    {
        float circleSize = 22.0f;
        float cx = (float)x + circleSize / 2.0f;
        float cy = (float)(h / 2);
        g.setColour(Colors::primary().withAlpha(0.18f));
        g.fillEllipse(cx - circleSize / 2.0f, cy - circleSize / 2.0f, circleSize, circleSize);
        g.setColour(Colors::primary().withAlpha(0.5f));
        g.drawEllipse(cx - circleSize / 2.0f, cy - circleSize / 2.0f, circleSize, circleSize, 1.0f);
        g.setColour(Colors::primary());
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText(juce::String(row + 1), (int)(cx - circleSize / 2.0f), (int)(cy - circleSize / 2.0f),
                   (int)circleSize, (int)circleSize, juce::Justification::centred);
    }
    x += 28;

    g.setColour(selected ? juce::Colours::white : Colors::textPrimary());
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText(t.title, x, 0, 192, h, juce::Justification::centredLeft, true);
    x += 196;

    g.setColour(Colors::textSecondary());
    g.setFont(juce::Font(11.5f, juce::Font::bold));
    g.drawText(t.artist, x, 0, 142, h, juce::Justification::centredLeft, true);
    x += 146;

    {
        juce::String bpmStr = t.bpm > 0.0f ? juce::String(t.bpm, 1) : "-";
        float pillW = 56.0f;
        float pillH = 24.0f;
        juce::Rectangle<float> bpmRect((float)x, (float)(h / 2) - pillH / 2.0f, pillW, pillH);
        g.setColour(bpmBlue.withAlpha(0.20f));
        g.fillRoundedRectangle(bpmRect, 12.0f);
        g.setColour(bpmBlue.withAlpha(0.40f));
        g.drawRoundedRectangle(bpmRect, 12.0f, 1.0f);
        g.setColour(bpmBlue);
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(bpmStr, bpmRect, juce::Justification::centred);
    }
    x += 60;

    {
        juce::String keyStr = t.key.isNotEmpty() ? t.key : "-";
        float pillW = 48.0f;
        float pillH = 24.0f;
        juce::Rectangle<float> keyRect((float)x, (float)(h / 2) - pillH / 2.0f, pillW, pillH);
        g.setColour(keyViolet.withAlpha(0.20f));
        g.fillRoundedRectangle(keyRect, 12.0f);
        g.setColour(keyViolet.withAlpha(0.40f));
        g.drawRoundedRectangle(keyRect, 12.0f, 1.0f);
        g.setColour(keyViolet);
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(keyStr, keyRect, juce::Justification::centred);
    }
    x += 52;

    {
        float energyNorm = t.energy / 10.0f;
        int barW = 52;
        float barH = 8.0f;
        float barY = (float)(h / 2) - barH / 2.0f;

        g.setColour(Colors::bgLightest().withAlpha(0.35f));
        g.fillRoundedRectangle((float)x, barY, (float)barW, barH, 3.0f);

        juce::Colour eCol = energyNorm < 0.4f ? Colors::success()
                           : energyNorm < 0.7f ? Colors::warning()
                           : Colors::error();
        float fillW = barW * energyNorm;
        if (fillW > 0)
        {
            juce::ColourGradient eGrad(eCol.withAlpha(0.95f), (float)x, barY,
                                        eCol.withAlpha(0.55f), (float)(x + fillW), barY, false);
            g.setGradientFill(eGrad);
            g.fillRoundedRectangle((float)x, barY, fillW, barH, 3.0f);
        }

        juce::Rectangle<float> eNumRect((float)(x + barW + 2), (float)(h / 2 - 8), 18.0f, 16.0f);
        g.setColour(eCol.withAlpha(0.12f));
        g.fillRoundedRectangle(eNumRect, 3.0f);
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.setColour(eCol);
        g.drawText(t.energy > 0 ? juce::String(t.energy) : "-", eNumRect, juce::Justification::centred);
    }
    x += 76;

    {
        juce::String durStr;
        if (t.durationSec > 0.0) {
            int mins = (int)(t.durationSec / 60.0);
            int secs = (int)t.durationSec % 60;
            durStr = juce::String::formatted("%d:%02d", mins, secs);
        } else {
            durStr = "--:--";
        }
        juce::Rectangle<float> durRect((float)x, (float)(h / 2 - 9), 40.0f, 18.0f);
        g.setColour(Colors::bgLightest().withAlpha(0.2f));
        g.fillRoundedRectangle(durRect, 3.0f);
        g.setColour(Colors::textMuted());
        g.setFont(juce::Font("Consolas", 10.0f, juce::Font::bold));
        g.drawText(durStr, durRect, juce::Justification::centred);
    }
    x += 44;

    if (owner && row < (int)tracks->size() - 1)
    {
        float compat = owner->computeTrackCompatibility(row, row + 1);
        juce::Colour cCol = compat >= 70.0f ? Colors::success()
                          : compat >= 40.0f ? Colors::warning()
                          : Colors::error();

        float dotCx = (float)x + 4.0f;
        float dotCy = (float)(h / 2);
        g.setColour(cCol.withAlpha(0.15f));
        g.fillEllipse(dotCx - 7, dotCy - 7, 14, 14);
        g.setColour(cCol);
        g.fillEllipse(dotCx - 4, dotCy - 4, 8, 8);
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.fillEllipse(dotCx - 1.5f, dotCy - 2.0f, 3, 2.5f);

        g.setColour(cCol);
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText(juce::String((int)compat) + "%", x + 12, 0, 32, h, juce::Justification::centredLeft);
    }

    g.setColour(Colors::border().withAlpha(0.2f));
    g.drawHorizontalLine(h - 1, 0.0f, (float)w);
}

void PlaylistPreparationView::TrackListModel::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    if (!tracks || row < 0 || row >= (int)tracks->size() || !owner) return;
    owner->m_listeners.call(&Listener::trackPreviewRequested, (*tracks)[row].filePath);
}

juce::var PlaylistPreparationView::TrackListModel::getDragSourceDescription(
    const juce::SparseSet<int>& selectedRows)
{
    return "PlaylistTrackDrag:" + juce::String(selectedRows.isEmpty() ? -1 : selectedRows[0]);
}

void PlaylistPreparationView::DragDropTrackListBox::mouseDrag(const juce::MouseEvent& e)
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
                container->startDragging(juce::var(row), this);
        }
    }
}

void PlaylistPreparationView::DragDropTrackListBox::itemDragMove(
    const juce::DragAndDropTarget::SourceDetails& details)
{
    int insertRow = getInsertionIndexForPosition(details.localPosition.x, details.localPosition.y);
    if (insertRow != dragInsertIndex)
    {
        dragInsertIndex = insertRow;
        repaint();
    }
}

void PlaylistPreparationView::DragDropTrackListBox::itemDragExit(
    const juce::DragAndDropTarget::SourceDetails&)
{
    dragInsertIndex = -1;
    repaint();
}

void PlaylistPreparationView::DragDropTrackListBox::itemDropped(
    const juce::DragAndDropTarget::SourceDetails& details)
{
    dragInsertIndex = -1;
    dragSourceRow = -1;

    if (details.description.isString())
    {
        const juce::String payload = details.description.toString();
        if (onExternalDrop && payload.isNotEmpty())
            onExternalDrop(payload);
        repaint();
        return;
    }

    int fromRow = details.description.isInt() ? (int)details.description : -1;
    int toRow = getInsertionIndexForPosition(details.localPosition.x, details.localPosition.y);

    if (fromRow >= 0 && toRow >= 0 && fromRow != toRow && onReorder)
        onReorder(fromRow, toRow);

    repaint();
}

void PlaylistPreparationView::DragDropTrackListBox::paint(juce::Graphics& g)
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

void PlaylistPreparationView::CompatibilityBadge::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced(2.0f);
    juce::Colour col;
    switch (level)
    {
        case Good:    col = Colors::success(); break;
        case Warning: col = Colors::warning(); break;
        case Bad:     col = Colors::error();   break;
    }
    g.setColour(col.withAlpha(0.15f));
    g.fillRoundedRectangle(b, 4.0f);
    g.setColour(col);
    g.drawRoundedRectangle(b, 4.0f, 1.0f);

    float dotSz = 6.0f;
    g.fillEllipse(b.getCentreX() - dotSz * 0.5f - 20.0f, b.getCentreY() - dotSz * 0.5f, dotSz, dotSz);

    g.setFont(juce::Font(9.0f));
    g.drawText(juce::String(percent) + "% " + hint, b.reduced(4.0f), juce::Justification::centred);
}

void PlaylistPreparationView::PlaylistScoreDisplay::paint(juce::Graphics& g)
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
    float cy = 55.0f;
    float radius = 36.0f;
    float arcThickness = 7.0f;
    float startAngle = -juce::MathConstants<float>::pi * 0.75f;
    float endAngle   =  juce::MathConstants<float>::pi * 0.75f;
    float totalArc   = endAngle - startAngle;

    juce::Path arcBg;
    arcBg.addCentredArc(cx, cy, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour(Colors::bgLightest().withAlpha(0.5f));
    g.strokePath(arcBg, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    float scoreNorm = score / 100.0f;
    if (score > 0)
    {
        int numSegs = juce::jmax(2, (int)(scoreNorm * 24));
        for (int s = 0; s < numSegs; ++s)
        {
            float t0 = s / (float)numSegs;
            float t1 = (s + 1) / (float)numSegs;
            float a0 = startAngle + t0 * scoreNorm * totalArc;
            float a1 = startAngle + t1 * scoreNorm * totalArc;

            float tMid = (t0 + t1) * 0.5f;
            juce::Colour segCol;
            if (tMid < 0.4f)
                segCol = Colors::error().interpolatedWith(Colors::warning(), tMid / 0.4f);
            else if (tMid < 0.7f)
                segCol = Colors::warning().interpolatedWith(juce::Colour(0xFF22C55E), (tMid - 0.4f) / 0.3f);
            else
                segCol = juce::Colour(0xFF22C55E).interpolatedWith(juce::Colour(0xFF34D399), (tMid - 0.7f) / 0.3f);

            juce::Path seg;
            seg.addCentredArc(cx, cy, radius, radius, 0.0f, a0, a1, true);
            g.setColour(segCol);
            g.strokePath(seg, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
        }

        float tipAngle = startAngle + scoreNorm * totalArc;
        float tipX = cx + radius * std::cos(tipAngle);
        float tipY = cy + radius * std::sin(tipAngle);
        juce::Colour tipCol = score >= 80 ? Colors::success() : score >= 50 ? Colors::warning() : Colors::error();
        g.setColour(tipCol.withAlpha(0.25f));
        g.fillEllipse(tipX - 8, tipY - 8, 16, 16);
        g.setColour(tipCol.withAlpha(0.5f));
        g.fillEllipse(tipX - 4, tipY - 4, 8, 8);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.fillEllipse(tipX - 2, tipY - 2, 4, 4);
    }

    juce::Colour scoreCol = score >= 80 ? Colors::success() : score >= 50 ? Colors::warning() : Colors::error();
    g.setColour(scoreCol.withAlpha(0.10f));
    g.fillEllipse(cx - radius - 16, cy - radius - 16, (radius + 16) * 2, (radius + 16) * 2);
    g.setColour(scoreCol.withAlpha(0.06f));
    g.fillEllipse(cx - radius - 24, cy - radius - 24, (radius + 24) * 2, (radius + 24) * 2);
    g.setColour(scoreCol.withAlpha(0.15f));
    g.fillEllipse(cx - radius - 8, cy - radius - 8, (radius + 8) * 2, (radius + 8) * 2);
    g.setColour(scoreCol.withAlpha(0.18f));
    g.setFont(juce::Font(28.0f, juce::Font::bold));
    g.drawText(juce::String(score), (int)(cx - 30), (int)(cy - 16), 60, 32, juce::Justification::centred);
    g.setFont(juce::Font(24.0f, juce::Font::bold));
    g.setColour(Colors::textPrimary());
    g.drawText(juce::String(score), (int)(cx - 28), (int)(cy - 14), 56, 28, juce::Justification::centred);

    g.setFont(juce::Font(10.0f));
    g.setColour(Colors::textMuted());
    g.drawText("Score Playlist", b.getX(), cy + radius + 4, b.getWidth(), 14,
               juce::Justification::centred);

    float dy = cy + radius + 24;
    auto drawBar = [&](const juce::String& label, int val, juce::Colour c, float yy)
    {
        g.setFont(juce::Font(10.0f));
        g.setColour(Colors::textMuted());
        g.drawText(label, 12.0f, yy, 65.0f, 14.0f, juce::Justification::centredLeft);
        float barX = 80.0f;
        float barW = b.getWidth() - barX - 40.0f;
        float barH = 8.0f;

        g.setColour(Colors::bgLightest().withAlpha(0.4f));
        g.fillRoundedRectangle(barX, yy + 3.0f, barW, barH, 4.0f);

        float fillW = barW * (val / 100.0f);
        if (fillW > 0)
        {
            juce::ColourGradient barGrad(c.withAlpha(0.9f), barX, yy + 3.0f,
                                          c.withAlpha(0.5f), barX + fillW, yy + 3.0f, false);
            g.setGradientFill(barGrad);
            g.fillRoundedRectangle(barX, yy + 3.0f, fillW, barH, 4.0f);

            g.setColour(juce::Colours::white.withAlpha(0.1f));
            g.fillRoundedRectangle(barX, yy + 3.0f, fillW, barH * 0.4f, 2.0f);
        }

        g.setColour(val >= 70 ? Colors::textPrimary() : Colors::textSecondary());
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText(juce::String(val), barX + barW + 4.0f, yy, 28.0f, 14.0f,
                   juce::Justification::centredLeft);
    };
    drawBar("BPM Flow", bpmScore, Colors::primary(), dy);
    drawBar("Key Harm.", keyScore, Colors::accent(), dy + 20.0f);
    drawBar("Energy", energyScore, Colors::warning(), dy + 40.0f);
    drawBar("Diversity", diversityScore, Colors::secondary(), dy + 60.0f);

    if (suggestion.isNotEmpty())
    {
        float sy = dy + 88.0f;
        g.setColour(Colors::bgLighter().withAlpha(0.8f));
        g.fillRoundedRectangle(8.0f, sy, b.getWidth() - 16.0f, 36.0f, 6.0f);
        g.setColour(Colors::info().withAlpha(0.6f));
        g.fillRoundedRectangle(8.0f, sy, 3.0f, 36.0f, 1.5f);
        g.setColour(Colors::info().withAlpha(0.15f));
        g.drawRoundedRectangle(8.0f, sy, b.getWidth() - 16.0f, 36.0f, 6.0f, 1.0f);

        g.setColour(Colors::info());
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText("Suggestion:", 16.0f, sy + 2.0f, 70.0f, 14.0f, juce::Justification::centredLeft);
        g.setColour(Colors::textSecondary());
        g.setFont(juce::Font(9.0f));
        g.drawFittedText(suggestion, 16, (int)(sy + 16), (int)(b.getWidth() - 32), 18,
                         juce::Justification::centredLeft, 2);
    }
}

PlaylistPreparationView::SmartRuleEditor::SmartRuleEditor()
{
    titleLabel = std::make_unique<juce::Label>("t", BM_TJ("playlistPrep.smartPlaylist"));
    titleLabel->setFont(juce::Font(16.0f, juce::Font::bold));
    titleLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    addAndMakeVisible(*titleLabel);

    addRuleBtn = std::make_unique<juce::TextButton>("+ Ajouter une regle");
    addRuleBtn->setColour(juce::TextButton::buttonColourId, Colors::primary());
    addRuleBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    addRuleBtn->onClick = [this] { addRule(); };
    addAndMakeVisible(*addRuleBtn);

    applyBtn = std::make_unique<juce::TextButton>(BM_TJ("common.apply"));
    applyBtn->setColour(juce::TextButton::buttonColourId, Colors::success());
    applyBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    applyBtn->onClick = [this] { applyRules(); };
    addAndMakeVisible(*applyBtn);

    closeBtn = std::make_unique<juce::TextButton>("X");
    closeBtn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFB91C1C));
    closeBtn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    closeBtn->onClick = [this]() { setVisible(false); };
    addAndMakeVisible(*closeBtn);

    addRule();
}

void PlaylistPreparationView::SmartRuleEditor::addRule()
{
    auto row = std::make_unique<RuleRow>();

    row->fieldCombo = std::make_unique<juce::ComboBox>();
    row->fieldCombo->addItem("Genre", 1);
    row->fieldCombo->addItem("BPM", 2);
    row->fieldCombo->addItem("Key", 3);
    row->fieldCombo->addItem("Energy", 4);
    row->fieldCombo->addItem("Rating", 5);
    row->fieldCombo->setSelectedId(1);
    row->fieldCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    row->fieldCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    addAndMakeVisible(*row->fieldCombo);

    row->opCombo = std::make_unique<juce::ComboBox>();
    row->opCombo->addItem("=", 1);
    row->opCombo->addItem(">", 2);
    row->opCombo->addItem("<", 3);
    row->opCombo->addItem("entre", 4);
    row->opCombo->addItem("contient", 5);
    row->opCombo->setSelectedId(1);
    row->opCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    row->opCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    addAndMakeVisible(*row->opCombo);

    row->valueEdit = std::make_unique<juce::TextEditor>();
    row->valueEdit->setColour(juce::TextEditor::backgroundColourId, Colors::bgLighter());
    row->valueEdit->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
    row->valueEdit->setTextToShowWhenEmpty("Valeur...", Colors::textDim());
    addAndMakeVisible(*row->valueEdit);

    row->value2Edit = std::make_unique<juce::TextEditor>();
    row->value2Edit->setColour(juce::TextEditor::backgroundColourId, Colors::bgLighter());
    row->value2Edit->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
    row->value2Edit->setTextToShowWhenEmpty("Max...", Colors::textDim());
    addAndMakeVisible(*row->value2Edit);

    int idx = (int)rules.size();
    row->removeBtn = std::make_unique<juce::TextButton>("X");
    row->removeBtn->setColour(juce::TextButton::buttonColourId, Colors::error().withAlpha(0.3f));
    row->removeBtn->setColour(juce::TextButton::textColourOffId, Colors::error());
    row->removeBtn->onClick = [this, idx] { removeRule(idx); };
    addAndMakeVisible(*row->removeBtn);

    rules.push_back(std::move(row));
    resized();
}

void PlaylistPreparationView::SmartRuleEditor::removeRule(int index)
{
    if (index >= 0 && index < (int)rules.size())
    {
        rules.erase(rules.begin() + index);
        for (int i = 0; i < (int)rules.size(); ++i)
        {
            rules[i]->removeBtn->onClick = [this, i] { removeRule(i); };
        }
        resized();
        repaint();
    }
}

void PlaylistPreparationView::SmartRuleEditor::applyRules()
{
    std::vector<SmartRule> result;
    for (auto& row : rules)
    {
        SmartRule r;
        r.field = row->fieldCombo->getText();
        switch (row->opCombo->getSelectedId())
        {
            case 1: r.op = "="; break;
            case 2: r.op = ">"; break;
            case 3: r.op = "<"; break;
            case 4: r.op = "between"; break;
            case 5: r.op = "contains"; break;
            default: r.op = "="; break;
        }
        r.value = row->valueEdit->getText();
        r.value2 = row->value2Edit->getText();
        result.push_back(r);
    }
    if (onApply) onApply(result);
}

void PlaylistPreparationView::SmartRuleEditor::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);

    {
        juce::ColourGradient bgGrad(Colors::bgDarker().withAlpha(0.97f), bounds.getCentreX(), bounds.getY(),
                                     Colors::bgSidebar().withAlpha(0.95f), bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill(bgGrad);
        g.fillRoundedRectangle(bounds, 12.0f);
    }
    g.setColour(Colors::border());
    g.drawRoundedRectangle(bounds, 12.0f, 2.0f);

    int y = 40;
    for (int i = 0; i < (int)rules.size(); ++i)
    {
        juce::Rectangle<float> pillRect(12.0f, (float)y - 3.0f, bounds.getWidth() - 24.0f, 34.0f);
        juce::Colour pillCol = (i % 2 == 0) ? Colors::bgCard() : Colors::bgSurface();
        g.setColour(pillCol.withAlpha(0.6f));
        g.fillRoundedRectangle(pillRect, 8.0f);

        juce::Colour accentCol;
        if (rules[i]->fieldCombo)
        {
            int fieldId = rules[i]->fieldCombo->getSelectedId();
            switch (fieldId)
            {
                case 1: accentCol = Colors::secondary(); break;    // Genre = purple
                case 2: accentCol = Colors::primary(); break;      // BPM = blue
                case 3: accentCol = Colors::accent(); break;       // Key = cyan
                case 4: accentCol = Colors::warning(); break;      // Energy = orange
                case 5: accentCol = Colors::starFilled(); break;   // Rating = gold
                default: accentCol = Colors::textDim(); break;
            }
        }
        else
        {
            accentCol = Colors::textDim();
        }
        g.setColour(accentCol.withAlpha(0.15f));
        g.fillEllipse(16.0f, (float)y + 5.0f, 8.0f, 8.0f);
        g.setColour(accentCol);
        g.fillEllipse(17.5f, (float)y + 6.5f, 5.0f, 5.0f);

        g.setColour(accentCol.withAlpha(0.15f));
        g.drawRoundedRectangle(pillRect, 8.0f, 0.5f);

        y += 34;
    }
}

void PlaylistPreparationView::SmartRuleEditor::resized()
{
    if (closeBtn) closeBtn->setBounds(getWidth() - 32, 4, 28, 24);
    int y = 8;
    titleLabel->setBounds(16, y, 300, 24);
    y += 32;

    for (auto& row : rules)
    {
        row->fieldCombo->setBounds(28, y, 86, 28);
        row->opCombo->setBounds(120, y, 76, 28);
        row->valueEdit->setBounds(202, y, 96, 28);
        row->value2Edit->setBounds(304, y, 76, 28);
        row->removeBtn->setBounds(386, y, 28, 28);
        y += 34;
    }

    addRuleBtn->setBounds(28, y, 155, 28);
    applyBtn->setBounds(195, y, 155, 28);
}

PlaylistPreparationView::IABuildDialog::IABuildDialog()
{
    titleLabel = std::make_unique<juce::Label>("t", BM_TJ("soiree.generate"));
    titleLabel->setFont(juce::Font(16.0f, juce::Font::bold));
    titleLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    addAndMakeVisible(*titleLabel);

    moodCombo = std::make_unique<juce::ComboBox>();
    moodCombo->addItem(BM_TJ("playlistPrep.settings.mood.chill"), 1);
    moodCombo->addItem(BM_TJ("playlistPrep.settings.mood.energetic"), 2);
    moodCombo->addItem(BM_TJ("playlistPrep.settings.mood.progressive"), 3);
    moodCombo->addItem(BM_TJ("playlistPrep.settings.mood.mixed"), 4);
    moodCombo->setTooltip(BM_TJ("playlistPrep.settings.moodTip"));
    moodCombo->setSelectedId(3);
    moodCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    moodCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    addAndMakeVisible(*moodCombo);

    durationCombo = std::make_unique<juce::ComboBox>();
    durationCombo->addItem("1h (60 min)", 1);
    durationCombo->addItem("2h (120 min)", 2);
    durationCombo->addItem("3h (180 min)", 3);
    durationCombo->setSelectedId(1);
    durationCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    durationCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    addAndMakeVisible(*durationCombo);

    buildBtn = std::make_unique<juce::TextButton>(BM_TJ("common.apply"));
    buildBtn->setColour(juce::TextButton::buttonColourId, Colors::secondary());
    buildBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    buildBtn->onClick = [this] {
        if (onBuild)
        {
            IABuildSettings s;
            s.mood = moodCombo->getSelectedId() - 1;
            switch (durationCombo->getSelectedId())
            {
                case 1: s.durationMin = 60; break;
                case 2: s.durationMin = 120; break;
                case 3: s.durationMin = 180; break;
                default: s.durationMin = 60; break;
            }
            onBuild(s);
        }
    };
    addAndMakeVisible(*buildBtn);

    cancelBtn = std::make_unique<juce::TextButton>(BM_TJ("common.cancel"));
    cancelBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    cancelBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    cancelBtn->onClick = [this]() { setVisible(false); };
    addAndMakeVisible(*cancelBtn);

    closeBtn = std::make_unique<juce::TextButton>("X");
    closeBtn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFB91C1C));
    closeBtn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    closeBtn->onClick = [this]() { setVisible(false); };
    addAndMakeVisible(*closeBtn);
}

void PlaylistPreparationView::IABuildDialog::paint(juce::Graphics& g)
{
    g.fillAll(Colors::bgDarker().withAlpha(0.95f));
    g.setColour(Colors::border());
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 12.0f, 2.0f);

    g.setFont(juce::Font(11.0f));
    g.setColour(Colors::textSecondary());
    g.drawText("Mood / Style:", 16, 42, 100, 20, juce::Justification::centredLeft);
    g.drawText("Duree cible:", 16, 78, 100, 20, juce::Justification::centredLeft);
}

void PlaylistPreparationView::IABuildDialog::resized()
{
    if (closeBtn) closeBtn->setBounds(getWidth() - 32, 4, 28, 24);
    titleLabel->setBounds(16, 8, 300, 24);
    moodCombo->setBounds(120, 40, 180, 28);
    durationCombo->setBounds(120, 76, 180, 28);
    buildBtn->setBounds(60, 120, 120, 32);
    cancelBtn->setBounds(190, 120, 100, 32);
}

// Detache le listener du library-browser avant destruction
PlaylistPreparationView::~PlaylistPreparationView()
{
    if (m_libraryBrowser && m_libListener)
        m_libraryBrowser->removeListener(m_libListener.get());
}

PlaylistPreparationView::PlaylistPreparationView()
    : m_provider(nullptr)
{
    setupUI();
    setupFilters();
    m_playlists.clear();
    m_playlistListModel->playlistNames.clear();
    m_playlistList->updateContent();
    m_currentPlaylistIndex = -1;
    m_trackListModel->tracks = nullptr;
    m_trackList->updateContent();
    updatePlaylistScore();
    retranslateUi();
}

PlaylistPreparationView::PlaylistPreparationView(Services::Library::TrackDataProvider* provider)
    : m_provider(provider)
{
    setupUI();
    setupFilters();

    if (m_provider)
    {
        reloadPlaylistsFromDb();

        auto genres = m_provider->getGenreDistribution();
        std::map<juce::String, int> famCounts;
        for (auto& [genre, count] : genres)
        {
            if (genre.empty()) continue;
            auto fam = genreFamily(juce::String::fromUTF8(genre.c_str()));
            if (fam.isNotEmpty()) famCounts[fam] += count;
        }
        std::vector<std::pair<juce::String, int>> fams(famCounts.begin(), famCounts.end());
        std::sort(fams.begin(), fams.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        int genreId = 2;
        for (auto& [fam, count] : fams)
            m_filterGenreCombo->addItem(fam + "  (" + juce::String(count) + ")", genreId++);
    }

    refreshPlaylistList();

    if (!m_playlists.empty())
    {
        m_currentPlaylistIndex = 0;
        m_trackListModel->tracks = &m_playlists[0].tracks;
        m_trackList->updateContent();
    }
    updatePlaylistScore();
    retranslateUi();
}

void PlaylistPreparationView::refreshPlaylistList()
{
    m_playlistListModel->playlistNames.clear();
    m_playlistListModel->isSmartFlags.clear();
    for (auto& p : m_playlists)
    {
        juce::String label = p.name;
        if (p.dbId > 0 && !label.contains("("))
            label += " (" + juce::String((int)p.tracks.size()) + ")";
        m_playlistListModel->playlistNames.add(label);
        m_playlistListModel->isSmartFlags.push_back(p.isSmart);
    }
    m_playlistList->updateContent();
    m_playlistList->repaint();
}

Models::Track PlaylistPreparationView::toModelTrack(const TrackInfo& ti) const
{
    Models::Track t;
    t.id = ti.trackId;
    t.title = ti.title.toStdString();
    t.artist = ti.artist.toStdString();
    t.key = ti.key.toStdString();
    t.genre = ti.genre.toStdString();
    t.filePath = ti.filePath.toStdString();
    t.bpm = ti.bpm;
    t.energy = static_cast<float>(ti.energy);
    t.duration = ti.durationSec;
    t.rating = ti.rating;
    return t;
}

std::vector<int64_t> PlaylistPreparationView::currentTrackIds() const
{
    std::vector<int64_t> ids;
    if (m_currentPlaylistIndex < 0 || m_currentPlaylistIndex >= (int)m_playlists.size())
        return ids;
    for (auto& t : m_playlists[m_currentPlaylistIndex].tracks)
        if (t.trackId > 0) ids.push_back(t.trackId);
    return ids;
}

void PlaylistPreparationView::reloadPlaylistsFromDb()
{
    if (!m_provider) return;

    m_allTracksCache.clear();
    auto dbAllTracks = m_provider->getAllTracks();
    for (auto& t : dbAllTracks)
    {
        m_allTracksCache.push_back({
            juce::String(t.title), juce::String(t.artist),
            juce::String(t.camelotKey.empty() ? t.key : t.camelotKey),
            juce::String(t.genre), juce::String(t.filePath),
            static_cast<float>(t.bpm), static_cast<int>(t.energy),
            t.duration, t.rating, t.id });
    }

    m_playlists.clear();
    Playlist allPl;
    allPl.name = "Toutes les pistes (" + juce::String((int)m_allTracksCache.size()) + ")";
    allPl.tracks = m_allTracksCache;
    allPl.dbId = -1;
    m_playlists.push_back(allPl);

    auto dbPlaylists = m_provider->getAllPlaylists();
    for (auto& pl : dbPlaylists)
    {
        Playlist p;
        p.name = juce::String(pl.name);
        p.dbId = pl.id;
        p.isSmart = pl.isSmartPlaylist;
        auto plTracks = m_provider->getPlaylistTracks(pl.id);
        for (auto& t : plTracks)
        {
            p.tracks.push_back({
                juce::String(t.title), juce::String(t.artist),
                juce::String(t.camelotKey.empty() ? t.key : t.camelotKey),
                juce::String(t.genre), juce::String(t.filePath),
                static_cast<float>(t.bpm), static_cast<int>(t.energy),
                t.duration, t.rating, t.id });
        }
        m_playlists.push_back(std::move(p));
    }
    refreshPlaylistList();
}

void PlaylistPreparationView::selectPlaylistByDbId(int64_t dbId)
{
    for (int i = 0; i < (int)m_playlists.size(); ++i)
    {
        if (m_playlists[i].dbId == dbId)
        {
            m_playlistListModel->selectedIndex = i;
            m_playlistList->selectRow(i);
            loadPlaylistTracks(i);
            return;
        }
    }
}

void PlaylistPreparationView::persistCurrentOrder()
{
    if (!m_provider || m_currentPlaylistIndex < 0
        || m_currentPlaylistIndex >= (int)m_playlists.size()) return;
    auto& pl = m_playlists[m_currentPlaylistIndex];
    if (pl.dbId <= 0) return;
    m_provider->reorderPlaylist(pl.dbId, currentTrackIds());
}

void PlaylistPreparationView::loadPlaylistTracks(int index)
{
    if (index >= 0 && index < (int)m_playlists.size())
    {
        m_currentPlaylistIndex = index;
        m_isFilterActive = false;
        m_filteredTracks.clear();

        auto& pl = m_playlists[index];
        if (m_provider && pl.isSmart && pl.dbId > 0)
        {
            auto matches = m_provider->refreshSmartPlaylist(pl.dbId);
            pl.tracks.clear();
            for (auto& t : matches)
            {
                pl.tracks.push_back({
                    juce::String(t.title), juce::String(t.artist),
                    juce::String(t.camelotKey.empty() ? t.key : t.camelotKey),
                    juce::String(t.genre), juce::String(t.filePath),
                    static_cast<float>(t.bpm), static_cast<int>(t.energy),
                    t.duration, t.rating, t.id });
            }
            refreshPlaylistList();
        }

        updateTrackListPointer();
        m_trackList->updateContent();
        updatePlaylistScore();
        repaint();
    }
}

void PlaylistPreparationView::PlaylistVisualizationToggle::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(Colors::bgLighter().withAlpha(0.6f));
    g.fillRoundedRectangle(b, 6.0f);
    g.setColour(Colors::border().withAlpha(0.5f));
    g.drawRoundedRectangle(b, 6.0f, 1.0f);

    const char* labels[] = { "List", "Graph", "Scatter" };
    float segW = b.getWidth() / 3.0f;
    for (int i = 0; i < 3; ++i)
    {
        juce::Rectangle<float> seg(b.getX() + segW * i, b.getY(), segW, b.getHeight());
        if ((int)currentMode == i)
        {
            g.setColour(Colors::primary().withAlpha(0.85f));
            g.fillRoundedRectangle(seg.reduced(2.0f), 4.0f);
            g.setColour(Colors::textPrimary());
        }
        else
        {
            g.setColour(Colors::textSecondary());
        }
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(labels[i], seg, juce::Justification::centred);
    }
}

void PlaylistPreparationView::PlaylistVisualizationToggle::mouseDown(const juce::MouseEvent& e)
{
    float segW = (float)getWidth() / 3.0f;
    int idx = juce::jlimit(0, 2, (int)((float)e.x / segW));
    Mode newMode = static_cast<Mode>(idx);
    if (newMode != currentMode)
    {
        currentMode = newMode;
        if (onModeChanged) onModeChanged(currentMode);
        repaint();
    }
}

void PlaylistPreparationView::updateVisualization()
{
    if (m_trackList) m_trackList->updateContent();
    repaint();
}

void PlaylistPreparationView::setupUI()
{
    m_titleLabel = std::make_unique<juce::Label>("t", BM_TJ("playlistPrep.title"));
    m_titleLabel->setFont(juce::Font(22.0f, juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId, Colors::primary());
    addAndMakeVisible(*m_titleLabel);

    auto makeBtn = [this](const juce::String& text, juce::Colour bg = Colors::bgLighter())
    {
        auto b = std::make_unique<juce::TextButton>(text);
        b->setColour(juce::TextButton::buttonColourId, bg);
        b->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        addAndMakeVisible(*b);
        return b;
    };

    m_newBtn       = makeBtn(juce::String::fromUTF8("+ Playlist"), Colors::primary());
    m_newBtn->setTooltip(juce::String::fromUTF8("Creer une playlist normale (editable manuellement)"));
    m_openBtn      = makeBtn("Ouvrir");
    m_saveBtn      = makeBtn("Sauvegarder", Colors::success().withAlpha(0.8f));
    m_deleteBtn    = makeBtn("Supprimer", Colors::error().withAlpha(0.6f));
    m_duplicateBtn = makeBtn("Dupliquer");
    m_iaBuildBtn   = makeBtn("IA Construire", Colors::secondary());
    m_smartPlaylistBtn = makeBtn(juce::String::fromUTF8("+ Smart"), Colors::accent());
    m_smartPlaylistBtn->setTooltip(juce::String::fromUTF8("Creer une smart playlist (regles auto, apercu live)"));
    m_addTracksBtn    = makeBtn("+ Ajouter", Colors::success());
    m_removeTracksBtn = makeBtn("- Retirer", Colors::error().withAlpha(0.6f));

    m_newBtn->onClick       = [this] { spdlog::info("[PlaylistPrepView] newPlaylist clicked"); onNewPlaylist(); };
    m_openBtn->onClick      = [this] { spdlog::info("[PlaylistPrepView] openPlaylist clicked"); onOpenPlaylist(); };
    m_saveBtn->onClick      = [this] { spdlog::info("[PlaylistPrepView] savePlaylist clicked"); onSavePlaylist(); };
    m_deleteBtn->onClick    = [this] { spdlog::info("[PlaylistPrepView] deletePlaylist clicked"); onDeletePlaylist(); };
    m_duplicateBtn->onClick = [this] { spdlog::info("[PlaylistPrepView] duplicatePlaylist clicked"); onDuplicatePlaylist(); };
    m_iaBuildBtn->onClick   = [this] { spdlog::info("[PlaylistPrepView] iaBuild clicked"); onIAAutoBuild(); };
    m_smartPlaylistBtn->onClick = [this] { spdlog::info("[PlaylistPrepView] smartPlaylist clicked"); onSmartPlaylistBuild(); };
    m_addTracksBtn->onClick = [this] {
        spdlog::info("[PlaylistPrepView] addTracks clicked");
        if (m_currentPlaylistIndex < 0 || m_currentPlaylistIndex >= (int)m_playlists.size()) {
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::InfoIcon)
                    .withTitle("Aucune playlist selectionnee")
                    .withMessage("Cree ou selectionne une playlist avant d'ajouter des morceaux.")
                    .withButton("OK"),
                nullptr);
            return;
        }
        if (m_libraryBrowser) {
            m_libraryBrowser->setVisible(true);
            m_libraryBrowser->toFront(true);
            resized();
            repaint();
        }
    };
    m_removeTracksBtn->onClick = [this] {
        spdlog::info("[PlaylistPrepView] removeTracks clicked");
        if (!m_trackList || m_currentPlaylistIndex < 0
            || m_currentPlaylistIndex >= (int)m_playlists.size()) return;
        int row = m_trackList->getSelectedRow();
        if (row < 0) {
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::InfoIcon)
                    .withTitle("Aucun morceau selectionne")
                    .withMessage("Selectionne un morceau dans la playlist avant de le retirer.")
                    .withButton("OK"),
                nullptr);
            return;
        }
        auto& pl = m_playlists[m_currentPlaylistIndex];
        if (row >= (int)pl.tracks.size()) return;
        int64_t removedId = pl.tracks[row].trackId;
        pl.tracks.erase(pl.tracks.begin() + row);
        if (m_provider && pl.dbId > 0 && removedId > 0)
            m_provider->removeFromPlaylist(pl.dbId, removedId);
        updateTrackListPointer();
        if (m_trackList) m_trackList->updateContent();
        updatePlaylistScore();
        refreshPlaylistList();
        repaint();
    };

    m_playlistListModel = std::make_unique<PlaylistListModel>();
    m_playlistListModel->onSelectionChanged = [this](int row) {
        loadPlaylistTracks(row);
    };
    m_playlistList = std::make_unique<juce::ListBox>("pl", m_playlistListModel.get());
    m_playlistList->setRowHeight(36);
    m_playlistList->setColour(juce::ListBox::backgroundColourId, Colors::bgDark());
    m_playlistList->setColour(juce::ListBox::outlineColourId, Colors::border());
    addAndMakeVisible(*m_playlistList);

    m_trackListModel = std::make_unique<TrackListModel>();
    m_trackListModel->owner = this;
    m_trackList = std::make_unique<DragDropTrackListBox>("tl", m_trackListModel.get());
    m_trackList->setRowHeight(34);
    m_trackList->setColour(juce::ListBox::backgroundColourId, Colors::bgDarker());
    m_trackList->setColour(juce::ListBox::outlineColourId, Colors::border());
    m_trackList->onReorder = [this](int from, int to) {
        if (m_isFilterActive || m_currentPlaylistIndex < 0) return;
        auto& tracks = m_playlists[m_currentPlaylistIndex].tracks;
        if (from < 0 || from >= (int)tracks.size() || to < 0 || to > (int)tracks.size()) return;
        auto item = tracks[from];
        tracks.erase(tracks.begin() + from);
        int insertAt = (to > from) ? to - 1 : to;
        if (insertAt > (int)tracks.size()) insertAt = (int)tracks.size();
        tracks.insert(tracks.begin() + insertAt, item);
        updateTrackListPointer();
        m_trackList->updateContent();
        updatePlaylistScore();
        updateCompatibilityIndicators();
        persistCurrentOrder();
    };
    addAndMakeVisible(*m_trackList);

    m_autoSortCombo = std::make_unique<juce::ComboBox>("sortMode");
    m_autoSortCombo->addItem(BM_TJ("playlistPrep.settings.sort.bpmProgressive"), 1);
    m_autoSortCombo->addItem(BM_TJ("playlistPrep.settings.sort.energyCurve"), 2);
    m_autoSortCombo->addItem(BM_TJ("playlistPrep.settings.sort.harmonic"), 3);
    m_autoSortCombo->addItem(BM_TJ("playlistPrep.settings.sort.aiOptimal"), 4);
    m_autoSortCombo->setSelectedId(1);
    m_autoSortCombo->setTooltip(BM_TJ("playlistPrep.settings.sortTip"));
    m_autoSortCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    m_autoSortCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_autoSortCombo->setColour(juce::ComboBox::outlineColourId, Colors::border());
    addAndMakeVisible(*m_autoSortCombo);

    m_autoSortBtn = makeBtn("Auto-Trier");
    m_autoSortBtn->onClick = [this] { onAutoSort(m_autoSortCombo->getSelectedId()); };
    m_fillGapsBtn = makeBtn("Boucher trous", Colors::accent());
    m_fillGapsBtn->onClick = [this] { onFillGaps(); };

    m_suggestBtn = makeBtn(juce::String::fromUTF8("Suggestions IA"), Colors::secondary());
    m_suggestBtn->setTooltip(juce::String::fromUTF8("Proposer des morceaux compatibles depuis la bibliotheque"));
    m_suggestBtn->onClick = [this] { onSuggestAI(); };
    m_fillBtn = makeBtn(juce::String::fromUTF8("Remplir"), Colors::accent());
    m_fillBtn->setTooltip(juce::String::fromUTF8("Completer la playlist jusqu'a une duree cible"));
    m_fillBtn->onClick = [this] { onFillPlaylist(); };
    m_sendDJBtn = makeBtn(juce::String::fromUTF8("Envoyer vers DJ"), Colors::primary());
    m_sendDJBtn->setTooltip(juce::String::fromUTF8("Envoyer la playlist vers le logiciel DJ detecte"));
    m_sendDJBtn->onClick = [this] { onSendToDJ(); };

    m_scoreDisplay = std::make_unique<PlaylistScoreDisplay>();
    m_scoreDisplay->score = 0;
    m_scoreDisplay->bpmScore = 0;
    m_scoreDisplay->keyScore = 0;
    m_scoreDisplay->energyScore = 0;
    m_scoreDisplay->diversityScore = 0;
    addAndMakeVisible(*m_scoreDisplay);

    m_exportM3UBtn  = makeBtn("M3U");
    m_exportPLSBtn  = makeBtn("PLS");
    m_exportXSPFBtn = makeBtn("XSPF");
    m_exportPDFBtn  = makeBtn("HTML", Colors::primary());
    m_exportJSONBtn = makeBtn("JSON");

    m_exportM3UBtn->onClick  = [this] { spdlog::info("[PlaylistPrepView] exportM3U clicked"); onExport("M3U"); };
    m_exportPLSBtn->onClick  = [this] { spdlog::info("[PlaylistPrepView] exportPLS clicked"); onExport("PLS"); };
    m_exportXSPFBtn->onClick = [this] { spdlog::info("[PlaylistPrepView] exportXSPF clicked"); onExport("XSPF"); };
    m_exportPDFBtn->onClick  = [this] { spdlog::info("[PlaylistPrepView] exportPDF clicked"); onExport("PDF"); };
    m_exportJSONBtn->onClick = [this] { spdlog::info("[PlaylistPrepView] exportJSON clicked"); onExport("JSON"); };

    m_smartRuleEditor = std::make_unique<SmartRuleEditor>();
    m_smartRuleEditor->setVisible(false);
    m_smartRuleEditor->onApply = [this](std::vector<SmartRule> rules) {
        m_smartRules = rules;
        m_smartRuleEditor->setVisible(false);

        if (!m_provider) return;

        Playlist smartPl;
        smartPl.name = "Smart Playlist";
        smartPl.isSmart = true;

        for (auto& t : m_allTracksCache)
        {
            bool match = true;
            for (auto& rule : m_smartRules)
            {
                if (rule.field == "Genre")
                {
                    if (rule.op == "=" && t.genre.compareIgnoreCase(rule.value) != 0) match = false;
                    if (rule.op == "contains" && !t.genre.containsIgnoreCase(rule.value)) match = false;
                }
                else if (rule.field == "BPM")
                {
                    float val = rule.value.getFloatValue();
                    if (rule.op == "=" && std::abs(t.bpm - val) > 2.0f) match = false;
                    if (rule.op == ">" && t.bpm <= val) match = false;
                    if (rule.op == "<" && t.bpm >= val) match = false;
                    if (rule.op == "between")
                    {
                        float val2 = rule.value2.getFloatValue();
                        if (t.bpm < val || t.bpm > val2) match = false;
                    }
                }
                else if (rule.field == "Key")
                {
                    if (rule.op == "=" && t.key.compareIgnoreCase(rule.value) != 0) match = false;
                    if (rule.op == "contains" && !t.key.containsIgnoreCase(rule.value)) match = false;
                }
                else if (rule.field == "Energy")
                {
                    int val = rule.value.getIntValue();
                    if (rule.op == "=" && t.energy != val) match = false;
                    if (rule.op == ">" && t.energy <= val) match = false;
                    if (rule.op == "<" && t.energy >= val) match = false;
                    if (rule.op == "between")
                    {
                        int val2 = rule.value2.getIntValue();
                        if (t.energy < val || t.energy > val2) match = false;
                    }
                }
                else if (rule.field == "Rating")
                {
                    int val = rule.value.getIntValue();
                    if (rule.op == ">" && t.rating <= val) match = false;
                    if (rule.op == "=" && t.rating != val) match = false;
                }
            }
            if (match) smartPl.tracks.push_back(t);
        }

        m_playlists.push_back(smartPl);
        refreshPlaylistList();
        loadPlaylistTracks((int)m_playlists.size() - 1);
        repaint();
    };
    addChildComponent(*m_smartRuleEditor);

    m_nestedSmartEditor = std::make_unique<Widgets::NestedSmartRuleEditor>();
    m_nestedSmartEditor->setVisible(false);
    m_nestedSmartEditor->onCancel = [this] {
        m_nestedSmartEditor->setVisible(false);
        repaint();
    };
    m_nestedSmartEditor->onLivePreview = [this](const Models::SmartPlaylistRuleGroup& g) -> juce::String {
        if (!m_provider) return juce::String::fromUTF8("0 morceau correspond");
        int n = m_provider->countSmartMatches(g);
        return juce::String(n) + juce::String::fromUTF8(n > 1 ? " morceaux correspondent"
                                                              : " morceau correspond");
    };
    m_nestedSmartEditor->onApply = [this](const Models::SmartPlaylistRuleGroup& group) {
        m_nestedSmartEditor->setVisible(false);

        if (!m_provider) { repaint(); return; }

        auto pendingGroup = std::make_shared<Models::SmartPlaylistRuleGroup>(group);
        auto* nameWin = new juce::AlertWindow(
            juce::String::fromUTF8("Nouvelle smart playlist"),
            juce::String::fromUTF8("Donne un nom a la smart playlist :"),
            juce::MessageBoxIconType::QuestionIcon, this);
        nameWin->addTextEditor("name", juce::String::fromUTF8("Smart Playlist"));
        nameWin->addButton(juce::String::fromUTF8("Creer"), 1, juce::KeyPress(juce::KeyPress::returnKey));
        nameWin->addButton(juce::String::fromUTF8("Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
        nameWin->enterModalState(true, juce::ModalCallbackFunction::create(
            [this, nameWin, pendingGroup](int result) {
                juce::String name = nameWin->getTextEditorContents("name").trim();
                std::unique_ptr<juce::AlertWindow> cleanup(nameWin);
                if (result != 1 || name.isEmpty()) { repaint(); return; }

                int64_t id = m_provider->createSmartPlaylist(name.toStdString(), *pendingGroup);
                if (id <= 0) {
                    juce::AlertWindow::showAsync(
                        juce::MessageBoxOptions()
                            .withIconType(juce::MessageBoxIconType::WarningIcon)
                            .withTitle(juce::String::fromUTF8("Echec creation"))
                            .withMessage(juce::String::fromUTF8("La smart playlist n'a pas pu etre enregistree."))
                            .withButton("OK"), nullptr);
                    return;
                }
                reloadPlaylistsFromDb();
                selectPlaylistByDbId(id);
                repaint();
            }), false);
    };
    addChildComponent(*m_nestedSmartEditor);

    m_iaBuildDialog = std::make_unique<IABuildDialog>();
    m_iaBuildDialog->setVisible(false);
    m_iaBuildDialog->cancelBtn->onClick = [this] { m_iaBuildDialog->setVisible(false); repaint(); };
    m_iaBuildDialog->onBuild = [this](IABuildSettings settings) {
        m_iaBuildDialog->setVisible(false);

        if (m_allTracksCache.empty()) return;

        Playlist iaPl;
        juce::String moodNames[] = { "Chill", "Energetic", "Progressive", "Mixed" };
        iaPl.name = "IA " + moodNames[settings.mood] + " " + juce::String(settings.durationMin) + "min";

        int eMin = 1, eMax = 10;
        switch (settings.mood)
        {
            case 0: eMin = 1; eMax = 5; break;   // Chill
            case 1: eMin = 6; eMax = 10; break;   // Energetic
            case 2: eMin = 1; eMax = 10; break;   // Progressive (full range)
            case 3: eMin = 1; eMax = 10; break;   // Mixed
        }

        std::vector<TrackInfo> candidates;
        for (auto& t : m_allTracksCache)
        {
            if (t.energy >= eMin && t.energy <= eMax && t.bpm > 0)
                candidates.push_back(t);
        }

        if (candidates.empty()) return;

        if (settings.mood == 2) // Progressive
        {
            std::sort(candidates.begin(), candidates.end(),
                      [](const TrackInfo& a, const TrackInfo& b) { return a.energy < b.energy; });
        }
        else
        {
            if (settings.mood == 3)
            {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::shuffle(candidates.begin(), candidates.end(), gen);
            }
            else
            {
                std::sort(candidates.begin(), candidates.end(),
                          [](const TrackInfo& a, const TrackInfo& b) { return a.bpm < b.bpm; });
            }
        }

        double totalDuration = 0.0;
        double targetDuration = settings.durationMin * 60.0;
        std::set<int64_t> usedIds;

        if (!candidates.empty())
        {
            int startIdx = 0;
            if (settings.mood == 2) startIdx = 0; // lowest energy first
            else if (settings.mood == 1) startIdx = (int)candidates.size() / 2; // mid-high
            else startIdx = 0;

            iaPl.tracks.push_back(candidates[startIdx]);
            totalDuration += candidates[startIdx].durationSec;
            usedIds.insert(candidates[startIdx].trackId);
        }

        while (totalDuration < targetDuration && iaPl.tracks.size() < candidates.size())
        {
            const auto& last = iaPl.tracks.back();
            float bestScore = -1.0f;
            int bestIdx = -1;

            for (int i = 0; i < (int)candidates.size(); ++i)
            {
                if (usedIds.count(candidates[i].trackId)) continue;
                const auto& c = candidates[i];

                float bpmDiff = std::abs(last.bpm - c.bpm);
                float bpmS = juce::jmax(0.0f, 40.0f - bpmDiff * 4.0f);
                float keyS = areKeysCompatible(last.key, c.key) ? 35.0f : 0.0f;
                float eDiff = (float)std::abs(last.energy - c.energy);
                float eS = juce::jmax(0.0f, 25.0f - eDiff * 5.0f);

                float progressBonus = 0.0f;
                if (settings.mood == 2)
                {
                    if (c.energy >= last.energy) progressBonus = 5.0f;
                }

                float total = bpmS + keyS + eS + progressBonus;
                if (total > bestScore)
                {
                    bestScore = total;
                    bestIdx = i;
                }
            }

            if (bestIdx < 0) break;

            iaPl.tracks.push_back(candidates[bestIdx]);
            totalDuration += candidates[bestIdx].durationSec;
            usedIds.insert(candidates[bestIdx].trackId);
        }

        m_playlists.push_back(iaPl);
        refreshPlaylistList();
        loadPlaylistTracks((int)m_playlists.size() - 1);
        repaint();
    };
    addChildComponent(*m_iaBuildDialog);

    m_libraryBrowser = std::make_unique<LibraryBrowserPanel>(m_provider);
    addAndMakeVisible(*m_libraryBrowser);

    struct PlaylistLibListener : public LibraryBrowserPanel::Listener
    {
        PlaylistPreparationView* owner;
        PlaylistLibListener(PlaylistPreparationView* o) : owner(o) {}
        void trackDoubleClicked(const Models::Track& track) override
        {
            owner->addTrackToPlaylist(track);
        }
        void addTrackRequested(const Models::Track& track) override
        {
            owner->addTrackToPlaylist(track);
        }
    };
    m_libListener = std::make_unique<PlaylistLibListener>(this);
    m_libraryBrowser->addListener(m_libListener.get());

    m_vizToggle = std::make_unique<PlaylistVisualizationToggle>();
    m_vizToggle->currentMode = PlaylistVisualizationToggle::ListView;
    m_vizToggle->onModeChanged = [this](PlaylistVisualizationToggle::Mode m) {
        if (m_vizToggle) m_vizToggle->currentMode = m;
        updateVisualization();
        repaint();
    };
    m_vizToggle->setVisible(false);
    addChildComponent(*m_vizToggle);

    if (m_autoSortCombo)
    {
        m_autoSortCombo->onChange = [this] {
            if (m_autoSortCombo) {
                Prefs::setInt("playlistPrep.autoSortId", m_autoSortCombo->getSelectedId());
                onAutoSort(m_autoSortCombo->getSelectedId());
            }
        };
        const int autoSortId = Prefs::getInt("playlistPrep.autoSortId", 0);
        if (autoSortId >= 1 && autoSortId <= m_autoSortCombo->getNumItems())
            m_autoSortCombo->setSelectedId(autoSortId, juce::dontSendNotification);
    }

    if (m_trackList)
    {
        m_trackList->onExternalDrop = [this](const juce::String& payload) {
            handleLibraryDrop(payload);
        };
    }
}

void PlaylistPreparationView::retranslateUi()
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
        m_titleLabel->setText(BM_TJ("playlistPrep.title"), juce::dontSendNotification);

    if (m_autoSortCombo)
    {
        rebuild(m_autoSortCombo.get(), { "playlistPrep.settings.sort.bpmProgressive",
                                          "playlistPrep.settings.sort.energyCurve",
                                          "playlistPrep.settings.sort.harmonic",
                                          "playlistPrep.settings.sort.aiOptimal" });
        m_autoSortCombo->setTooltip(BM_TJ("playlistPrep.settings.sortTip"));
    }

    if (m_filterLabel)
        m_filterLabel->setText(BM_TJ("playlistPrep.filter"), juce::dontSendNotification);
    if (m_filterSearchEdit)
        m_filterSearchEdit->setTextToShowWhenEmpty(BM_TJ("library.search") + "...", Colors::textDim());
    if (m_filterResetBtn)
        m_filterResetBtn->setButtonText(BM_TJ("settings.reset"));

    if (m_smartRuleEditor)
    {
        if (m_smartRuleEditor->titleLabel)
            m_smartRuleEditor->titleLabel->setText(BM_TJ("playlistPrep.smartPlaylist"),
                                                   juce::dontSendNotification);
        if (m_smartRuleEditor->applyBtn)
            m_smartRuleEditor->applyBtn->setButtonText(BM_TJ("common.apply"));
    }

    if (m_iaBuildDialog)
    {
        if (m_iaBuildDialog->titleLabel)
            m_iaBuildDialog->titleLabel->setText(BM_TJ("soiree.generate"),
                                                 juce::dontSendNotification);
        rebuild(m_iaBuildDialog->moodCombo.get(), { "playlistPrep.settings.mood.chill",
                                                    "playlistPrep.settings.mood.energetic",
                                                    "playlistPrep.settings.mood.progressive",
                                                    "playlistPrep.settings.mood.mixed" });
        if (m_iaBuildDialog->moodCombo)
            m_iaBuildDialog->moodCombo->setTooltip(BM_TJ("playlistPrep.settings.moodTip"));
        if (m_iaBuildDialog->buildBtn)
            m_iaBuildDialog->buildBtn->setButtonText(BM_TJ("common.apply"));
        if (m_iaBuildDialog->cancelBtn)
            m_iaBuildDialog->cancelBtn->setButtonText(BM_TJ("common.cancel"));
    }

    repaint();
}

void PlaylistPreparationView::addTrackToPlaylist(const Models::Track& track)
{
    if (m_currentPlaylistIndex < 0 || m_currentPlaylistIndex >= (int)m_playlists.size())
    {
        if (m_playlists.empty())
        {
            Playlist p;
            p.name = "Nouvelle playlist 1";
            if (m_provider) p.dbId = m_provider->createPlaylist(p.name.toStdString());
            m_playlists.push_back(std::move(p));
            refreshPlaylistList();
        }
        m_currentPlaylistIndex = (int)m_playlists.size() - 1;
        updateTrackListPointer();
    }

    if (m_playlists[m_currentPlaylistIndex].dbId <= 0)
    {
        int target = -1;
        for (int i = (int)m_playlists.size() - 1; i >= 0; --i)
            if (m_playlists[i].dbId > 0) { target = i; break; }
        if (target < 0 && m_provider)
        {
            Playlist p;
            p.name = "Nouvelle playlist " + juce::String((int)m_playlists.size());
            p.dbId = m_provider->createPlaylist(p.name.toStdString());
            if (p.dbId > 0)
            {
                m_playlists.push_back(std::move(p));
                target = (int)m_playlists.size() - 1;
                refreshPlaylistList();
            }
        }
        if (target >= 0)
        {
            m_currentPlaylistIndex = target;
            if (m_playlistListModel) m_playlistListModel->selectedIndex = target;
            if (m_playlistList) m_playlistList->selectRow(target);
            m_isFilterActive = false;
            m_filteredTracks.clear();
            updateTrackListPointer();
        }
    }

    auto& pl = m_playlists[m_currentPlaylistIndex];

    for (auto& t : pl.tracks)
        if (t.trackId == track.id) return;

    pl.tracks.push_back({
        juce::String(track.title),
        juce::String(track.artist),
        juce::String(track.camelotKey.empty() ? track.key : track.camelotKey),
        juce::String(track.genre),
        juce::String(track.filePath),
        static_cast<float>(track.bpm),
        static_cast<int>(track.energy),
        track.duration,
        track.rating,
        track.id
    });

    updateTrackListPointer();
    if (m_trackList) m_trackList->updateContent();
    updatePlaylistScore();
    updateCompatibilityIndicators();

    if (m_provider && pl.dbId > 0)
        m_provider->addToPlaylist(pl.dbId, track.id);

    refreshPlaylistList();
}

void PlaylistPreparationView::handleLibraryDrop(const juce::String& trackIdsStr)
{
    if (!m_provider || m_currentPlaylistIndex < 0) return;

    auto idsPart = trackIdsStr.fromFirstOccurrenceOf("BEATMATE_TRACKS:", false, false);
    juce::StringArray idStrings;
    idStrings.addTokens(idsPart, ",", "");

    for (auto& idStr : idStrings)
    {
        int64_t trackId = idStr.getLargeIntValue();
        if (trackId <= 0) continue;

        auto track = m_provider->getTrack(trackId);
        if (track.id > 0)
            addTrackToPlaylist(track);
    }
}

void PlaylistPreparationView::onNewPlaylist()
{
    juce::String baseName = "Nouvelle playlist " + juce::String((int)m_playlists.size());

    if (!m_provider)
    {
        Playlist p;
        p.name = baseName;
        m_playlists.push_back(p);
        refreshPlaylistList();
        loadPlaylistTracks((int)m_playlists.size() - 1);
        return;
    }

    auto* win = new juce::AlertWindow(
        juce::String::fromUTF8("Nouvelle playlist"),
        juce::String::fromUTF8("Nom de la playlist :"),
        juce::MessageBoxIconType::QuestionIcon, this);
    win->addTextEditor("name", baseName);
    win->addButton(juce::String::fromUTF8("Creer"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    win->addButton(juce::String::fromUTF8("Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    win->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, win](int result) {
            juce::String name = win->getTextEditorContents("name").trim();
            std::unique_ptr<juce::AlertWindow> cleanup(win);
            if (result != 1 || name.isEmpty()) return;

            int64_t id = m_provider->createPlaylist(name.toStdString());
            if (id <= 0) return;
            reloadPlaylistsFromDb();
            selectPlaylistByDbId(id);
        }), false);
}

void PlaylistPreparationView::onOpenPlaylist()
{
    juce::FileChooser chooser("Ouvrir une playlist", juce::File(), "*.m3u;*.pls;*.json");
    if (chooser.browseForFileToOpen())
    {
        auto file = chooser.getResult();
        Playlist p;
        p.name = file.getFileNameWithoutExtension();

        if (file.hasFileExtension("m3u"))
        {
            auto lines = juce::StringArray::fromLines(file.loadFileAsString());
            for (auto& line : lines)
            {
                if (line.startsWith("#") || line.trim().isEmpty()) continue;
                juce::File trackFile(line.trim());
                if (trackFile.existsAsFile())
                {
                    TrackInfo ti;
                    ti.title = trackFile.getFileNameWithoutExtension();
                    ti.filePath = trackFile.getFullPathName();
                    p.tracks.push_back(ti);
                }
            }
        }

        m_playlists.push_back(p);
        refreshPlaylistList();
        loadPlaylistTracks((int)m_playlists.size() - 1);
    }
}

void PlaylistPreparationView::onSavePlaylist()
{
    if (m_currentPlaylistIndex < 0 || m_currentPlaylistIndex >= (int)m_playlists.size()) {
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle("Aucune playlist a sauvegarder")
                .withMessage("Cree ou selectionne une playlist avant de sauvegarder.")
                .withButton("OK"),
            nullptr);
        return;
    }

    auto& pl = m_playlists[m_currentPlaylistIndex];

    if (pl.dbId <= 0)
    {
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle(juce::String::fromUTF8("Rien a sauvegarder"))
                .withMessage(juce::String::fromUTF8("\xc2\xab Toutes les pistes \xc2\xbb n'est pas une playlist. "
                                                    "Cree une playlist (bouton Nouvelle), ajoute tes morceaux "
                                                    "dedans, puis sauvegarde."))
                .withButton("OK"),
            nullptr);
        return;
    }

    bool dbOk = true;
    if (m_provider)
        dbOk = m_provider->setPlaylistTracks(pl.dbId, currentTrackIds());

    {
        auto appDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("BeatMate");
        if (!appDir.isDirectory()) appDir.createDirectory();
        auto file = appDir.getChildFile("playlists_backup.json");

        nlohmann::json j;
        j["playlists"] = nlohmann::json::array();
        for (auto& p : m_playlists)
        {
            nlohmann::json jp;
            jp["name"] = p.name.toStdString();
            jp["dbId"] = p.dbId;
            jp["isSmart"] = p.isSmart;
            jp["tracks"] = nlohmann::json::array();
            for (auto& t : p.tracks)
            {
                nlohmann::json jt;
                jt["title"] = t.title.toStdString();
                jt["artist"] = t.artist.toStdString();
                jt["key"] = t.key.toStdString();
                jt["genre"] = t.genre.toStdString();
                jt["filePath"] = t.filePath.toStdString();
                jt["bpm"] = t.bpm;
                jt["energy"] = t.energy;
                jt["durationSec"] = t.durationSec;
                jt["rating"] = t.rating;
                jt["trackId"] = t.trackId;
                jp["tracks"].push_back(jt);
            }
            j["playlists"].push_back(jp);
        }
        file.replaceWithText(juce::String(j.dump(2)));
    }

    m_listeners.call(&Listener::playlistSaved);

    if (dbOk)
        refreshPlaylistList();

    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(dbOk ? juce::MessageBoxIconType::InfoIcon
                               : juce::MessageBoxIconType::WarningIcon)
            .withTitle(dbOk ? juce::String::fromUTF8("Playlist sauvegardee")
                            : juce::String::fromUTF8("Echec de sauvegarde"))
            .withMessage(dbOk
                ? (juce::String("\"") + pl.name + "\" : "
                   + juce::String((int)pl.tracks.size())
                   + juce::String::fromUTF8(" morceaux enregistres."))
                : juce::String::fromUTF8("La base n'a pas pu etre mise a jour. "
                                         "Une sauvegarde JSON locale a ete creee."))
            .withButton("OK"),
        nullptr);
}

void PlaylistPreparationView::onDeletePlaylist()
{
    if (m_currentPlaylistIndex <= 0 || m_currentPlaylistIndex >= (int)m_playlists.size()) return;
    // Ne jamais supprimer "Toutes les pistes" (index 0)

    auto& pl = m_playlists[m_currentPlaylistIndex];
    juce::String plName = pl.name;
    int64_t dbId = pl.dbId;

    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::QuestionIcon)
            .withTitle(juce::String::fromUTF8("Supprimer la playlist"))
            .withMessage(juce::String::fromUTF8("Supprimer definitivement \"") + plName + "\" ?")
            .withButton(juce::String::fromUTF8("Supprimer"))
            .withButton(juce::String::fromUTF8("Annuler")),
        [this, dbId, idx = m_currentPlaylistIndex](int result) {
            if (result != 1) return;
            if (m_provider && dbId > 0)
                m_provider->deletePlaylist(dbId);

            if (idx > 0 && idx < (int)m_playlists.size())
                m_playlists.erase(m_playlists.begin() + idx);
            refreshPlaylistList();
            m_currentPlaylistIndex = juce::jmax(0, juce::jmin(idx - 1,
                                                              (int)m_playlists.size() - 1));
            if (!m_playlists.empty())
                loadPlaylistTracks(m_currentPlaylistIndex);
            else
            {
                m_trackListModel->tracks = nullptr;
                m_trackList->updateContent();
            }
            repaint();
        });
}

void PlaylistPreparationView::onDuplicatePlaylist()
{
    if (m_currentPlaylistIndex < 0 || m_currentPlaylistIndex >= (int)m_playlists.size()) return;

    Playlist source = m_playlists[m_currentPlaylistIndex];
    juce::String dupName = source.name.upToFirstOccurrenceOf(" (", false, false) + " (copie)";

    if (!m_provider)
    {
        Playlist dup = source;
        dup.name = dupName;
        dup.dbId = -1;
        dup.isSmart = false;
        m_playlists.push_back(std::move(dup));
        refreshPlaylistList();
        loadPlaylistTracks((int)m_playlists.size() - 1);
        return;
    }

    int64_t newId = m_provider->createPlaylist(dupName.toStdString());
    if (newId <= 0) return;

    std::vector<int64_t> ids;
    for (auto& t : source.tracks)
        if (t.trackId > 0) ids.push_back(t.trackId);
    m_provider->setPlaylistTracks(newId, ids);

    reloadPlaylistsFromDb();
    selectPlaylistByDbId(newId);
}

void PlaylistPreparationView::onSuggestAI()
{
    if (!m_provider || m_currentPlaylistIndex < 0
        || m_currentPlaylistIndex >= (int)m_playlists.size()) return;

    auto ids = currentTrackIds();
    if (ids.empty())
    {
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle(juce::String::fromUTF8("Suggestions IA"))
                .withMessage(juce::String::fromUTF8("Ajoute au moins un morceau : "
                             "les suggestions se basent sur le contenu de la playlist."))
                .withButton("OK"), nullptr);
        return;
    }

    auto suggestions = m_provider->suggestForPlaylist(ids, 20);
    if (suggestions.empty())
    {
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle(juce::String::fromUTF8("Suggestions IA"))
                .withMessage(juce::String::fromUTF8("Aucune suggestion compatible trouvee "
                             "dans la bibliotheque."))
                .withButton("OK"), nullptr);
        return;
    }

    int added = 0;
    for (auto& t : suggestions)
        if (t.id > 0) { addTrackToPlaylist(t); ++added; }

    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::InfoIcon)
            .withTitle(juce::String::fromUTF8("Suggestions IA"))
            .withMessage(juce::String(added) + juce::String::fromUTF8(
                         " morceau(x) suggere(s) ajoute(s) a la playlist."))
            .withButton("OK"), nullptr);
}

void PlaylistPreparationView::onFillPlaylist()
{
    if (!m_provider || m_currentPlaylistIndex < 0
        || m_currentPlaylistIndex >= (int)m_playlists.size()) return;

    auto ids = currentTrackIds();
    if (ids.empty())
    {
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle(juce::String::fromUTF8("Remplir la playlist"))
                .withMessage(juce::String::fromUTF8("Ajoute un morceau de depart : "
                             "le remplissage prolonge la playlist par compatibilite."))
                .withButton("OK"), nullptr);
        return;
    }

    auto* win = new juce::AlertWindow(
        juce::String::fromUTF8("Remplir la playlist"),
        juce::String::fromUTF8("Duree cible (minutes) :"),
        juce::MessageBoxIconType::QuestionIcon, this);
    win->addTextEditor("dur", "60");
    win->addButton(juce::String::fromUTF8("Remplir"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    win->addButton(juce::String::fromUTF8("Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    win->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, win, ids](int result) {
            double minutes = win->getTextEditorContents("dur").getDoubleValue();
            std::unique_ptr<juce::AlertWindow> cleanup(win);
            if (result != 1 || minutes <= 0.0) return;

            auto fill = m_provider->suggestFillToDuration(ids, minutes);
            int added = 0;
            for (auto& t : fill)
                if (t.id > 0) { addTrackToPlaylist(t); ++added; }

            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::InfoIcon)
                    .withTitle(juce::String::fromUTF8("Remplir la playlist"))
                    .withMessage(juce::String(added) + juce::String::fromUTF8(
                                 " morceau(x) ajoute(s) pour atteindre ~")
                                 + juce::String((int)minutes) + " min.")
                    .withButton("OK"), nullptr);
        }), false);
}

void PlaylistPreparationView::onSendToDJ()
{
    if (!m_provider || m_currentPlaylistIndex < 0
        || m_currentPlaylistIndex >= (int)m_playlists.size()) return;

    std::vector<Models::Track> tracks;
    for (auto& t : m_playlists[m_currentPlaylistIndex].tracks)
        tracks.push_back(toModelTrack(t));

    if (tracks.empty())
    {
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle(juce::String::fromUTF8("Envoyer vers DJ"))
                .withMessage(juce::String::fromUTF8("La playlist est vide."))
                .withButton("OK"), nullptr);
        return;
    }

    std::string msg;
    bool ok = m_provider->sendTracksToDJ(tracks, msg);
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(ok ? juce::MessageBoxIconType::InfoIcon
                             : juce::MessageBoxIconType::WarningIcon)
            .withTitle(juce::String::fromUTF8("Envoyer vers DJ"))
            .withMessage(juce::String::fromUTF8(msg.c_str()))
            .withButton("OK"), nullptr);
}

void PlaylistPreparationView::onAutoSort(int mode)
{
    if (m_currentPlaylistIndex < 0 || m_currentPlaylistIndex >= (int)m_playlists.size()) return;
    auto& tracks = m_playlists[m_currentPlaylistIndex].tracks;
    if (tracks.size() < 2) return;

    switch (mode)
    {
        case 1: // BPM progressif
            std::sort(tracks.begin(), tracks.end(),
                      [](const TrackInfo& a, const TrackInfo& b) { return a.bpm < b.bpm; });
            break;
        case 2: // Energy curve
            std::sort(tracks.begin(), tracks.end(),
                      [](const TrackInfo& a, const TrackInfo& b) { return a.energy < b.energy; });
            break;
        case 3: // Harmonic (Camelot grouping)
            std::sort(tracks.begin(), tracks.end(),
                      [this](const TrackInfo& a, const TrackInfo& b) {
                          int na = camelotNumber(a.key), nb = camelotNumber(b.key);
                          if (na != nb) return na < nb;
                          return camelotLetter(a.key) < camelotLetter(b.key);
                      });
            break;
        case 4: // IA Optimal: greedy nearest-neighbor by compatibility
        {
            if (tracks.empty()) break;
            std::vector<TrackInfo> sorted;
            std::vector<bool> used(tracks.size(), false);
            sorted.push_back(tracks[0]);
            used[0] = true;

            for (size_t step = 1; step < tracks.size(); ++step)
            {
                const auto& last = sorted.back();
                float bestScore = -1.0f;
                int bestIdx = -1;

                for (size_t i = 0; i < tracks.size(); ++i)
                {
                    if (used[i]) continue;
                    float bpmDiff = std::abs(last.bpm - tracks[i].bpm);
                    float bpmS = juce::jmax(0.0f, 40.0f - bpmDiff * 4.0f);
                    float keyS = areKeysCompatible(last.key, tracks[i].key) ? 35.0f : 0.0f;
                    float eDiff = (float)std::abs(last.energy - tracks[i].energy);
                    float eS = juce::jmax(0.0f, 25.0f - eDiff * 5.0f);
                    float total = bpmS + keyS + eS;
                    if (total > bestScore) { bestScore = total; bestIdx = (int)i; }
                }

                if (bestIdx >= 0)
                {
                    sorted.push_back(tracks[bestIdx]);
                    used[bestIdx] = true;
                }
            }
            tracks = sorted;
            break;
        }
    }
    m_trackList->updateContent();
    updatePlaylistScore();
    persistCurrentOrder();
    repaint();
}

void PlaylistPreparationView::onFillGaps()
{
    if (!m_provider || m_currentPlaylistIndex < 0
        || m_currentPlaylistIndex >= (int)m_playlists.size()) return;

    m_isFilterActive = false;
    m_filteredTracks.clear();
    updateTrackListPointer();

    auto& tracks = m_playlists[m_currentPlaylistIndex].tracks;
    if (tracks.size() < 2) return;

    int maxInsertions = 5;
    int inserted = 0;

    while (inserted < maxInsertions)
    {
        float worstScore = 100.0f;
        int worstIdx = -1;
        for (int i = 0; i < (int)tracks.size() - 1; ++i)
        {
            float s = computeTrackCompatibility(i, i + 1);
            if (s < worstScore) { worstScore = s; worstIdx = i; }
        }

        if (worstIdx < 0 || worstScore >= 70.0f) break;

        float idealBpm = (tracks[worstIdx].bpm + tracks[worstIdx + 1].bpm) * 0.5f;
        int idealEnergy = (tracks[worstIdx].energy + tracks[worstIdx + 1].energy) / 2;

        float bestFitScore = -1.0f;
        int bestFitIdx = -1;

        for (int i = 0; i < (int)m_allTracksCache.size(); ++i)
        {
            auto& c = m_allTracksCache[i];
            bool alreadyIn = false;
            for (auto& t : tracks)
                if (t.trackId == c.trackId) { alreadyIn = true; break; }
            if (alreadyIn) continue;

            float bpmS = juce::jmax(0.0f, 40.0f - std::abs(c.bpm - idealBpm) * 4.0f);
            float eS = juce::jmax(0.0f, 25.0f - (float)std::abs(c.energy - idealEnergy) * 5.0f);
            float k1S = areKeysCompatible(tracks[worstIdx].key, c.key) ? 17.5f : 0.0f;
            float k2S = areKeysCompatible(c.key, tracks[worstIdx + 1].key) ? 17.5f : 0.0f;
            float total = bpmS + eS + k1S + k2S;

            if (total > bestFitScore) { bestFitScore = total; bestFitIdx = i; }
        }

        if (bestFitIdx < 0) break; // No suitable track found

        tracks.insert(tracks.begin() + worstIdx + 1, m_allTracksCache[bestFitIdx]);
        inserted++;
    }

    if (inserted > 0)
    {
        auto& pl = m_playlists[m_currentPlaylistIndex];
        if (m_provider && pl.dbId > 0)
            m_provider->setPlaylistTracks(pl.dbId, currentTrackIds());
        m_trackList->updateContent();
        updatePlaylistScore();
        repaint();
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle(juce::String::fromUTF8("Trous bouches"))
                .withMessage(juce::String(inserted)
                             + juce::String::fromUTF8(" morceau(x) de liaison ajoute(s) aux transitions les plus faibles."))
                .withButton("OK"),
            nullptr);
    }
    else
    {
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle(juce::String::fromUTF8("Aucun trou a boucher"))
                .withMessage(juce::String::fromUTF8("Toutes les transitions sont deja correctes (\xe2\x89\xa5 70 %), "
                                                    "ou aucun morceau de liaison compatible n'a ete trouve."))
                .withButton("OK"),
            nullptr);
    }
}

void PlaylistPreparationView::onSmartPlaylistBuild()
{
    if (m_nestedSmartEditor) {
        m_nestedSmartEditor->setGroup(m_nestedSmartEditor->getGroup());
        m_nestedSmartEditor->setVisible(true);
        m_nestedSmartEditor->toFront(true);
    }
    if (m_smartRuleEditor)  m_smartRuleEditor->setVisible(false);
    if (m_iaBuildDialog)    m_iaBuildDialog->setVisible(false);
    resized();
    repaint();
}

void PlaylistPreparationView::onIAAutoBuild()
{
    m_iaBuildDialog->setVisible(true);
    m_smartRuleEditor->setVisible(false);
    resized();
    repaint();
}

void PlaylistPreparationView::onExport(const juce::String& format)
{
    if (m_currentPlaylistIndex < 0 || m_currentPlaylistIndex >= (int)m_playlists.size()) return;
    if (m_exporting) return; // Re-entry guard: prevents double-chooser if user clicks twice fast.
    m_exporting = true;

    struct ExportGuard {
        bool& flag;
        ~ExportGuard() { flag = false; }
    } guard{ m_exporting };

    juce::String ext;
    if (format == "M3U") ext = "*.m3u";
    else if (format == "PLS") ext = "*.pls";
    else if (format == "XSPF") ext = "*.xspf";
    else if (format == "JSON") ext = "*.json";
    else if (format == "PDF") ext = "*.html"; // HTML setlist

    juce::FileChooser chooser("Exporter la playlist en " + format, juce::File(), ext);
    if (chooser.browseForFileToSave(true))
    {
        auto file = chooser.getResult();
        bool ok = true;

        if ((format == "M3U" || format == "PLS" || format == "XSPF") && m_provider)
        {
            auto& pl = m_playlists[m_currentPlaylistIndex];
            std::vector<Models::Track> tracks;
            for (auto& t : pl.tracks) tracks.push_back(toModelTrack(t));
            ok = m_provider->exportPlaylistToFile(
                tracks, pl.name.toStdString(),
                file.getFullPathName().toStdString(), format.toStdString());
        }
        else if (format == "M3U") writeM3U(file);
        else if (format == "PLS") writePLS(file);
        else if (format == "JSON") writeJSON(file);
        else if (format == "PDF") writePDF(file);

        m_listeners.call(&Listener::playlistExportRequested, format);

        if (!ok)
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::WarningIcon)
                    .withTitle(juce::String::fromUTF8("Echec export"))
                    .withMessage(juce::String::fromUTF8("Le fichier ") + format
                                 + juce::String::fromUTF8(" n'a pas pu etre ecrit."))
                    .withButton("OK"), nullptr);
    }
}

void PlaylistPreparationView::writeM3U(const juce::File& file)
{
    auto& tracks = m_playlists[m_currentPlaylistIndex].tracks;
    juce::String content = "#EXTM3U\n";
    for (auto& t : tracks)
    {
        int dur = (int)t.durationSec;
        content += "#EXTINF:" + juce::String(dur) + "," + t.artist + " - " + t.title + "\n";
        if (t.filePath.isNotEmpty())
            content += t.filePath + "\n";
        else
            content += t.title + "\n";
    }
    file.replaceWithText(content);
}

void PlaylistPreparationView::writePLS(const juce::File& file)
{
    auto& tracks = m_playlists[m_currentPlaylistIndex].tracks;
    juce::String content = "[playlist]\n";
    content += "NumberOfEntries=" + juce::String((int)tracks.size()) + "\n\n";
    for (int i = 0; i < (int)tracks.size(); ++i)
    {
        auto& t = tracks[i];
        content += "File" + juce::String(i + 1) + "=" + (t.filePath.isNotEmpty() ? t.filePath : t.title) + "\n";
        content += "Title" + juce::String(i + 1) + "=" + t.artist + " - " + t.title + "\n";
        content += "Length" + juce::String(i + 1) + "=" + juce::String((int)t.durationSec) + "\n\n";
    }
    content += "Version=2\n";
    file.replaceWithText(content);
}

void PlaylistPreparationView::writeJSON(const juce::File& file)
{
    auto& pl = m_playlists[m_currentPlaylistIndex];
    juce::String content = "{\n";
    content += "  \"name\": \"" + pl.name.replace("\"", "\\\"") + "\",\n";
    content += "  \"trackCount\": " + juce::String((int)pl.tracks.size()) + ",\n";
    content += "  \"tracks\": [\n";
    for (int i = 0; i < (int)pl.tracks.size(); ++i)
    {
        auto& t = pl.tracks[i];
        int mins = (int)(t.durationSec / 60.0);
        int secs = (int)t.durationSec % 60;
        content += "    {\n";
        content += "      \"index\": " + juce::String(i + 1) + ",\n";
        content += "      \"title\": \"" + t.title.replace("\"", "\\\"") + "\",\n";
        content += "      \"artist\": \"" + t.artist.replace("\"", "\\\"") + "\",\n";
        content += "      \"bpm\": " + juce::String(t.bpm, 1) + ",\n";
        content += "      \"key\": \"" + t.key + "\",\n";
        content += "      \"energy\": " + juce::String(t.energy) + ",\n";
        content += "      \"duration\": \"" + juce::String::formatted("%d:%02d", mins, secs) + "\",\n";
        content += "      \"filePath\": \"" + t.filePath.replace("\\", "\\\\").replace("\"", "\\\"") + "\"\n";
        content += "    }" + juce::String(i < (int)pl.tracks.size() - 1 ? "," : "") + "\n";
    }
    content += "  ]\n}\n";
    file.replaceWithText(content);
}

void PlaylistPreparationView::writePDF(const juce::File& file)
{
    auto& pl = m_playlists[m_currentPlaylistIndex];
    juce::String content;
    content += "<!DOCTYPE html>\n<html lang=\"fr\">\n<head>\n";
    content += "<meta charset=\"UTF-8\">\n";
    content += "<title>Setlist - " + pl.name + "</title>\n";
    content += "<style>\n";
    content += "  body { font-family: 'Segoe UI', Arial, sans-serif; background: #0f172a; color: #e2e8f0; padding: 40px; }\n";
    content += "  h1 { color: #3b82f6; border-bottom: 2px solid #3b82f6; padding-bottom: 10px; }\n";
    content += "  h2 { color: #94a3b8; font-size: 14px; margin-top: 5px; }\n";
    content += "  table { width: 100%; border-collapse: collapse; margin-top: 20px; }\n";
    content += "  th { background: #1e293b; color: #94a3b8; text-align: left; padding: 10px 12px; font-size: 12px; text-transform: uppercase; }\n";
    content += "  td { padding: 8px 12px; border-bottom: 1px solid #1e293b; }\n";
    content += "  tr:nth-child(even) { background: #1e293b40; }\n";
    content += "  tr:hover { background: #3b82f620; }\n";
    content += "  .bpm { color: #3b82f6; font-weight: bold; }\n";
    content += "  .key { color: #8b5cf6; font-weight: bold; }\n";
    content += "  .energy { color: #f59e0b; }\n";
    content += "  .score { text-align: center; margin: 20px 0; padding: 15px; background: #1e293b; border-radius: 8px; }\n";
    content += "  .score-value { font-size: 32px; color: #3b82f6; font-weight: bold; }\n";
    content += "  .footer { margin-top: 20px; padding-top: 10px; border-top: 1px solid #334155; color: #64748b; font-size: 13px; }\n";
    content += "</style>\n</head>\n<body>\n";
    content += "<h1>SETLIST: " + pl.name + "</h1>\n";
    content += "<div class=\"score\"><span class=\"score-value\">" + juce::String(m_scoreDisplay->score)
             + "/100</span><br>Score Playlist</div>\n";
    content += "<table>\n<thead><tr>";
    content += "<th>#</th><th>Titre</th><th>Artiste</th><th>BPM</th><th>Key</th><th>Energy</th><th>Duree</th>";
    content += "</tr></thead>\n<tbody>\n";

    double totalDur = 0;
    for (int i = 0; i < (int)pl.tracks.size(); ++i)
    {
        auto& t = pl.tracks[i];
        int mins = (int)(t.durationSec / 60.0);
        int secs = (int)t.durationSec % 60;
        totalDur += t.durationSec;

        content += "<tr>";
        content += "<td>" + juce::String(i + 1) + "</td>";
        content += "<td>" + t.title + "</td>";
        content += "<td>" + t.artist + "</td>";
        content += "<td class=\"bpm\">" + (t.bpm > 0 ? juce::String(t.bpm, 1) : "-") + "</td>";
        content += "<td class=\"key\">" + (t.key.isNotEmpty() ? t.key : "-") + "</td>";
        content += "<td class=\"energy\">" + (t.energy > 0 ? juce::String(t.energy) : "-") + "</td>";
        content += "<td>" + juce::String(mins) + ":" + juce::String::formatted("%02d", secs) + "</td>";
        content += "</tr>\n";
    }

    content += "</tbody>\n</table>\n";

    int totalMins = (int)(totalDur / 60.0);
    int totalSecs = (int)totalDur % 60;
    content += "<div class=\"footer\">";
    content += "<strong>Duree totale:</strong> " + juce::String(totalMins) + ":" +
              juce::String::formatted("%02d", totalSecs);
    content += " | <strong>Pistes:</strong> " + juce::String((int)pl.tracks.size());
    content += " | BeatMate V12 Professional";
    content += "</div>\n</body>\n</html>\n";

    juce::File outFile = file.hasFileExtension("html") ? file : file.withFileExtension("html");
    outFile.replaceWithText(content);
}

void PlaylistPreparationView::updatePlaylistScore()
{
    if (m_currentPlaylistIndex < 0 || m_currentPlaylistIndex >= (int)m_playlists.size())
    {
        m_scoreDisplay->score = 0;
        m_scoreDisplay->repaint();
        return;
    }
    auto& tracks = m_playlists[m_currentPlaylistIndex].tracks;
    if (tracks.size() < 2)
    {
        m_scoreDisplay->score = 0;
        m_scoreDisplay->suggestion = "Ajoutez des pistes pour obtenir un score.";
        m_scoreDisplay->repaint();
        return;
    }

    float bpmTotal = 0;
    for (size_t i = 1; i < tracks.size(); ++i)
    {
        float diff = std::abs(tracks[i].bpm - tracks[i - 1].bpm);
        bpmTotal += juce::jmax(0.0f, 100.0f - diff * 5.0f);
    }
    int bpmS = (int)(bpmTotal / (tracks.size() - 1));

    int keyCompat = 0;
    for (size_t i = 1; i < tracks.size(); ++i)
    {
        if (areKeysCompatible(tracks[i - 1].key, tracks[i].key))
            keyCompat++;
    }
    int keyS = (int)(100.0f * keyCompat / (tracks.size() - 1));

    float eTotal = 0;
    for (size_t i = 1; i < tracks.size(); ++i)
    {
        int diff = std::abs(tracks[i].energy - tracks[i - 1].energy);
        eTotal += juce::jmax(0.0f, 100.0f - diff * 15.0f);
    }
    int eS = (int)(eTotal / (tracks.size() - 1));

    std::set<juce::String> genres, artists;
    for (auto& t : tracks)
    {
        if (t.genre.isNotEmpty()) genres.insert(t.genre.toLowerCase());
        if (t.artist.isNotEmpty()) artists.insert(t.artist.toLowerCase());
    }
    int divS = juce::jmin(100, (int)(genres.size() * 15 + artists.size() * 5));

    m_scoreDisplay->bpmScore = juce::jlimit(0, 100, bpmS);
    m_scoreDisplay->keyScore = juce::jlimit(0, 100, keyS);
    m_scoreDisplay->energyScore = juce::jlimit(0, 100, eS);
    m_scoreDisplay->diversityScore = juce::jlimit(0, 100, divS);
    m_scoreDisplay->score = (bpmS + keyS + eS + divS) / 4;
    m_scoreDisplay->suggestion = getSuggestionText();
    m_scoreDisplay->repaint();
}

void PlaylistPreparationView::updateCompatibilityIndicators()
{
    // Compatibility indicators are drawn in real-time by TrackListModel::paintListBoxItem
    if (m_trackList) m_trackList->repaint();
}

void PlaylistPreparationView::setupFilters()
{
    m_filterLabel = std::make_unique<juce::Label>("fl", BM_TJ("playlistPrep.filter"));
    m_filterLabel->setFont(juce::Font("Segoe UI", 11.0f, juce::Font::bold));
    m_filterLabel->setColour(juce::Label::textColourId, Colors::textSecondary());
    m_filterLabel->setVisible(false);
    addChildComponent(*m_filterLabel);

    m_filterSearchEdit = std::make_unique<juce::TextEditor>("fs");
    m_filterSearchEdit->setTextToShowWhenEmpty(BM_TJ("library.search") + "...", Colors::textDim());
    m_filterSearchEdit->setColour(juce::TextEditor::backgroundColourId, Colors::bgLighter());
    m_filterSearchEdit->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
    m_filterSearchEdit->setColour(juce::TextEditor::outlineColourId, Colors::border());
    m_filterSearchEdit->onTextChange = [this]() { applyFilters(); };
    m_filterSearchEdit->setVisible(false);
    addChildComponent(*m_filterSearchEdit);

    m_filterBpmMin = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    m_filterBpmMin->setRange(60, 200, 1);
    m_filterBpmMin->setValue(60, juce::dontSendNotification);
    m_filterBpmMin->setColour(juce::Slider::trackColourId, Colors::primary());
    m_filterBpmMin->setColour(juce::Slider::backgroundColourId, Colors::bgLighter());
    m_filterBpmMin->onValueChange = [this]() {
        if (m_filterBpmMin->getValue() > m_filterBpmMax->getValue())
            m_filterBpmMax->setValue(m_filterBpmMin->getValue(), juce::dontSendNotification);
        m_filterBpmLabel->setText(juce::String((int)m_filterBpmMin->getValue()) + "-" +
                                  juce::String((int)m_filterBpmMax->getValue()) + " BPM", juce::dontSendNotification);
        Prefs::setInt("playlistPrep.bpmMin", (int)m_filterBpmMin->getValue());
        Prefs::setInt("playlistPrep.bpmMax", (int)m_filterBpmMax->getValue());
        applyFilters();
    };
    m_filterBpmMin->setVisible(false);
    addChildComponent(*m_filterBpmMin);

    m_filterBpmMax = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    m_filterBpmMax->setRange(60, 200, 1);
    m_filterBpmMax->setValue(200, juce::dontSendNotification);
    m_filterBpmMax->setColour(juce::Slider::trackColourId, Colors::primary());
    m_filterBpmMax->setColour(juce::Slider::backgroundColourId, Colors::bgLighter());
    m_filterBpmMax->onValueChange = [this]() {
        if (m_filterBpmMax->getValue() < m_filterBpmMin->getValue())
            m_filterBpmMin->setValue(m_filterBpmMax->getValue(), juce::dontSendNotification);
        m_filterBpmLabel->setText(juce::String((int)m_filterBpmMin->getValue()) + "-" +
                                  juce::String((int)m_filterBpmMax->getValue()) + " BPM", juce::dontSendNotification);
        Prefs::setInt("playlistPrep.bpmMin", (int)m_filterBpmMin->getValue());
        Prefs::setInt("playlistPrep.bpmMax", (int)m_filterBpmMax->getValue());
        applyFilters();
    };
    m_filterBpmMax->setVisible(false);
    addChildComponent(*m_filterBpmMax);

    m_filterBpmLabel = std::make_unique<juce::Label>("fbpm", "60-200 BPM");
    m_filterBpmLabel->setFont(juce::Font("Segoe UI", 10.0f, juce::Font::bold));
    m_filterBpmLabel->setColour(juce::Label::textColourId, Colors::primary());
    m_filterBpmLabel->setVisible(false);
    addChildComponent(*m_filterBpmLabel);

    m_filterKeyCombo = std::make_unique<juce::ComboBox>("fkey");
    m_filterKeyCombo->addItem("Toutes", 1);
    juce::StringArray keys = {"1A","1B","2A","2B","3A","3B","4A","4B","5A","5B","6A","6B",
                               "7A","7B","8A","8B","9A","9B","10A","10B","11A","11B","12A","12B"};
    for (int i = 0; i < keys.size(); ++i) m_filterKeyCombo->addItem(keys[i], i + 2);
    m_filterKeyCombo->setSelectedId(1, juce::dontSendNotification);
    m_filterKeyCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    m_filterKeyCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_filterKeyCombo->onChange = [this]() {
        Prefs::setInt("playlistPrep.keyFilterId", m_filterKeyCombo->getSelectedId());
        applyFilters();
    };
    m_filterKeyCombo->setVisible(false);
    addChildComponent(*m_filterKeyCombo);

    m_filterGenreCombo = std::make_unique<juce::ComboBox>("fgenre");
    m_filterGenreCombo->addItem("Tous", 1);
    m_filterGenreCombo->setSelectedId(1, juce::dontSendNotification);
    m_filterGenreCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    m_filterGenreCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_filterGenreCombo->onChange = [this]() {
        Prefs::setString("playlistPrep.genreFilter", m_filterGenreCombo->getText().toStdString());
        applyFilters();
    };
    m_filterGenreCombo->setVisible(false);
    addChildComponent(*m_filterGenreCombo);

    m_filterEnergyMin = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    m_filterEnergyMin->setRange(1, 10, 1);
    m_filterEnergyMin->setValue(1, juce::dontSendNotification);
    m_filterEnergyMin->setColour(juce::Slider::trackColourId, Colors::warning());
    m_filterEnergyMin->setColour(juce::Slider::backgroundColourId, Colors::bgLighter());
    m_filterEnergyMin->onValueChange = [this]() {
        if (m_filterEnergyMin->getValue() > m_filterEnergyMax->getValue())
            m_filterEnergyMax->setValue(m_filterEnergyMin->getValue(), juce::dontSendNotification);
        m_filterEnergyLabel->setText("E:" + juce::String((int)m_filterEnergyMin->getValue()) + "-" +
                                     juce::String((int)m_filterEnergyMax->getValue()), juce::dontSendNotification);
        Prefs::setInt("playlistPrep.energyMin", (int)m_filterEnergyMin->getValue());
        Prefs::setInt("playlistPrep.energyMax", (int)m_filterEnergyMax->getValue());
        applyFilters();
    };
    m_filterEnergyMin->setVisible(false);
    addChildComponent(*m_filterEnergyMin);

    m_filterEnergyMax = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    m_filterEnergyMax->setRange(1, 10, 1);
    m_filterEnergyMax->setValue(10, juce::dontSendNotification);
    m_filterEnergyMax->setColour(juce::Slider::trackColourId, Colors::warning());
    m_filterEnergyMax->setColour(juce::Slider::backgroundColourId, Colors::bgLighter());
    m_filterEnergyMax->onValueChange = [this]() {
        if (m_filterEnergyMax->getValue() < m_filterEnergyMin->getValue())
            m_filterEnergyMin->setValue(m_filterEnergyMax->getValue(), juce::dontSendNotification);
        m_filterEnergyLabel->setText("E:" + juce::String((int)m_filterEnergyMin->getValue()) + "-" +
                                     juce::String((int)m_filterEnergyMax->getValue()), juce::dontSendNotification);
        Prefs::setInt("playlistPrep.energyMin", (int)m_filterEnergyMin->getValue());
        Prefs::setInt("playlistPrep.energyMax", (int)m_filterEnergyMax->getValue());
        applyFilters();
    };
    m_filterEnergyMax->setVisible(false);
    addChildComponent(*m_filterEnergyMax);

    m_filterEnergyLabel = std::make_unique<juce::Label>("fe", "E:1-10");
    m_filterEnergyLabel->setFont(juce::Font("Segoe UI", 10.0f, juce::Font::bold));
    m_filterEnergyLabel->setColour(juce::Label::textColourId, Colors::warning());
    m_filterEnergyLabel->setVisible(false);
    addChildComponent(*m_filterEnergyLabel);

    m_filterRatingCombo = std::make_unique<juce::ComboBox>("frating");
    m_filterRatingCombo->addItem("Toutes", 1);
    m_filterRatingCombo->addItem("1+", 2);
    m_filterRatingCombo->addItem("2+", 3);
    m_filterRatingCombo->addItem("3+", 4);
    m_filterRatingCombo->addItem("4+", 5);
    m_filterRatingCombo->addItem("5", 6);
    m_filterRatingCombo->setSelectedId(1, juce::dontSendNotification);
    m_filterRatingCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    m_filterRatingCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_filterRatingCombo->onChange = [this]() {
        Prefs::setInt("playlistPrep.ratingFilterId", m_filterRatingCombo->getSelectedId());
        applyFilters();
    };
    m_filterRatingCombo->setVisible(false);
    addChildComponent(*m_filterRatingCombo);

    m_filterResetBtn = std::make_unique<juce::TextButton>(BM_TJ("settings.reset"));
    m_filterResetBtn->setColour(juce::TextButton::buttonColourId, Colors::error().withAlpha(0.3f));
    m_filterResetBtn->setColour(juce::TextButton::textColourOffId, Colors::error());
    m_filterResetBtn->onClick = [this]() {
        m_filterSearchEdit->setText("", false);
        m_filterBpmMin->setValue(60, juce::dontSendNotification);
        m_filterBpmMax->setValue(200, juce::dontSendNotification);
        m_filterBpmLabel->setText("60-200 BPM", juce::dontSendNotification);
        m_filterKeyCombo->setSelectedId(1, juce::dontSendNotification);
        m_filterGenreCombo->setSelectedId(1, juce::dontSendNotification);
        m_filterEnergyMin->setValue(1, juce::dontSendNotification);
        m_filterEnergyMax->setValue(10, juce::dontSendNotification);
        m_filterEnergyLabel->setText("E:1-10", juce::dontSendNotification);
        m_filterRatingCombo->setSelectedId(1, juce::dontSendNotification);
        applyFilters();
    };
    m_filterResetBtn->setVisible(false);
    addChildComponent(*m_filterResetBtn);

    {
        const int bpmMin = Prefs::getInt("playlistPrep.bpmMin", 60);
        const int bpmMax = Prefs::getInt("playlistPrep.bpmMax", 200);
        m_filterBpmMin->setValue(bpmMin, juce::dontSendNotification);
        m_filterBpmMax->setValue(bpmMax, juce::dontSendNotification);
        m_filterBpmLabel->setText(juce::String(bpmMin) + "-" + juce::String(bpmMax) + " BPM",
                                  juce::dontSendNotification);
        const int eMin = Prefs::getInt("playlistPrep.energyMin", 1);
        const int eMax = Prefs::getInt("playlistPrep.energyMax", 10);
        m_filterEnergyMin->setValue(eMin, juce::dontSendNotification);
        m_filterEnergyMax->setValue(eMax, juce::dontSendNotification);
        m_filterEnergyLabel->setText("E:" + juce::String(eMin) + "-" + juce::String(eMax),
                                     juce::dontSendNotification);
        const int keyId = Prefs::getInt("playlistPrep.keyFilterId", 1);
        if (keyId >= 1 && keyId <= m_filterKeyCombo->getNumItems())
            m_filterKeyCombo->setSelectedId(keyId, juce::dontSendNotification);
        const int rateId = Prefs::getInt("playlistPrep.ratingFilterId", 1);
        if (rateId >= 1 && rateId <= m_filterRatingCombo->getNumItems())
            m_filterRatingCombo->setSelectedId(rateId, juce::dontSendNotification);
    }
}

void PlaylistPreparationView::updateTrackListPointer()
{
    if (m_isFilterActive)
        m_trackListModel->tracks = &m_filteredTracks;
    else if (m_currentPlaylistIndex >= 0 && m_currentPlaylistIndex < (int)m_playlists.size())
        m_trackListModel->tracks = &m_playlists[m_currentPlaylistIndex].tracks;
    else
        m_trackListModel->tracks = nullptr;
}

void PlaylistPreparationView::applyFilters()
{
    if (!m_provider || m_currentPlaylistIndex < 0) return;

    float bpmMin = static_cast<float>(m_filterBpmMin->getValue());
    float bpmMax = static_cast<float>(m_filterBpmMax->getValue());
    std::string keyFilter;
    if (m_filterKeyCombo->getSelectedId() > 1)
        keyFilter = m_filterKeyCombo->getText().toStdString();
    juce::String genreFamilyFilter;
    if (m_filterGenreCombo->getSelectedId() > 1)
        genreFamilyFilter = m_filterGenreCombo->getText()
                                .upToFirstOccurrenceOf("  (", false, false).trim();
    float energyMin = static_cast<float>(m_filterEnergyMin->getValue());
    float energyMax = static_cast<float>(m_filterEnergyMax->getValue());
    int ratingMin = 0;
    if (m_filterRatingCombo->getSelectedId() > 1)
        ratingMin = m_filterRatingCombo->getSelectedId() - 1;

    juce::String searchText = m_filterSearchEdit->getText();

    bool hasFilter = (bpmMin > 60 || bpmMax < 200 || !keyFilter.empty() ||
                     genreFamilyFilter.isNotEmpty() || energyMin > 1 || energyMax < 10 ||
                     ratingMin > 0 || searchText.isNotEmpty());

    if (!hasFilter)
    {
        m_isFilterActive = false;
        m_filteredTracks.clear();
        updateTrackListPointer();
        m_trackList->updateContent();
        updatePlaylistScore();
        return;
    }

    m_isFilterActive = true;
    m_filteredTracks.clear();

    std::vector<Models::Track> dbTracks;
    bool hasDbFilter = (bpmMin > 60 || bpmMax < 200 || !keyFilter.empty() ||
                       energyMin > 1 || energyMax < 10 || ratingMin > 0);

    if (hasDbFilter)
        dbTracks = m_provider->getTracksByFilter(bpmMin, bpmMax, keyFilter, "", energyMin, energyMax, ratingMin);
    else if (searchText.isNotEmpty())
        dbTracks = m_provider->searchTracks(searchText.toStdString());
    else
        dbTracks = m_provider->getAllTracks();

    for (auto& t : dbTracks)
    {
        if (genreFamilyFilter.isNotEmpty()
            && genreFamily(juce::String::fromUTF8(t.genre.c_str())) != genreFamilyFilter)
            continue;
        if (searchText.isNotEmpty() && hasDbFilter)
        {
            juce::String lower = searchText.toLowerCase();
            if (!juce::String(t.title).toLowerCase().contains(lower) &&
                !juce::String(t.artist).toLowerCase().contains(lower))
                continue;
        }
        m_filteredTracks.push_back({
            juce::String(t.title),
            juce::String(t.artist),
            juce::String(t.camelotKey.empty() ? t.key : t.camelotKey),
            juce::String(t.genre),
            juce::String(t.filePath),
            static_cast<float>(t.bpm),
            static_cast<int>(t.energy),
            t.duration,
            t.rating,
            t.id
        });
    }

    updateTrackListPointer();
    m_trackList->updateContent();
    updatePlaylistScore();
}

void PlaylistPreparationView::paint(juce::Graphics& g)
{
    juce::ColourGradient bgGrad(Colors::bgDarkest(), 0.0f, 0.0f,
                                Colors::bgDarker(), 0.0f, (float)getHeight(), false);
    g.setGradientFill(bgGrad);
    g.fillRect(getLocalBounds());

    auto drawGlassPanel = [&](juce::Rectangle<int> r, float alpha = 0.12f)
    {
        auto rf = r.toFloat();
        g.setColour(juce::Colours::white.withAlpha(alpha));
        g.fillRoundedRectangle(rf, 12.0f);
        g.setColour(Colors::border().withAlpha(0.4f));
        g.drawRoundedRectangle(rf, 12.0f, 1.0f);
        g.setColour(juce::Colour(0xFF0A0A12).withAlpha(0.3f));
        g.drawLine(rf.getX() + 12, rf.getBottom() - 0.5f, rf.getRight() - 12, rf.getBottom() - 0.5f, 1.0f);
    };

    // Layout constants MUST match resized() exactly
    int headerH = 56;
    int margin = 12;
    int bottomH = 48;
    int contentTop = headerH + 4;
    int contentH = getHeight() - contentTop - bottomH - 8;

    int W = getWidth();
    int libBrowserW = (W - margin * 5) * 25 / 100;   // matches resized()
    int playlistListW = 180;
    int rightW = 260;

    int plListX = margin + libBrowserW + margin;
    int centerX = plListX + playlistListW + 8;
    int centerW = W - (plListX + playlistListW) - rightW - margin * 2 - 8;
    int scoreX  = W - rightW - margin + 8;

    g.setFont(juce::Font("Segoe UI", 12.0f, juce::Font::plain));
    g.setColour(Colors::textMuted());
    g.drawText("Organisez et optimisez vos playlists DJ", plListX, 34, 350, 16, juce::Justification::centredLeft);

    drawGlassPanel({plListX, contentTop, playlistListW, contentH});
    drawGlassPanel({centerX, contentTop, centerW, contentH});
    drawGlassPanel({scoreX - 8, contentTop, rightW - 8, contentH});
    drawGlassPanel({margin, getHeight() - bottomH - 4, W - margin * 2, bottomH - 4}, 0.05f);

    ProDraw::separator(g, (float)(centerX - 4), (float)(contentTop + 20), 4.0f);

    g.setColour(Colors::primary());
    g.fillRoundedRectangle((float)(plListX + 4), (float)(contentTop + 4), 4.0f, 16.0f, 2.0f);
    g.setFont(juce::Font("Segoe UI", 12.0f, juce::Font::bold));
    g.setColour(Colors::textPrimary());
    g.drawText("PLAYLISTS", plListX + 12, contentTop + 4, playlistListW - 20, 16, juce::Justification::centredLeft);

    g.setColour(Colors::accent());
    g.fillRoundedRectangle((float)(centerX + 4), (float)(contentTop + 4), 4.0f, 16.0f, 2.0f);
    g.setFont(juce::Font("Segoe UI", 12.0f, juce::Font::bold));
    g.setColour(Colors::textPrimary());
    g.drawText("PISTES", centerX + 12, contentTop + 4, 100, 16, juce::Justification::centredLeft);

    {
        g.setColour(Colors::success());
        g.fillRoundedRectangle((float)(scoreX + 4), (float)(contentTop + 4), 4.0f, 16.0f, 2.0f);
        g.setFont(juce::Font("Segoe UI", 12.0f, juce::Font::bold));
        g.setColour(Colors::textPrimary());
        g.drawText("SCORE", scoreX + 12, contentTop + 4, 100, 16, juce::Justification::centredLeft);
    }

    if (m_currentPlaylistIndex >= 0 && m_currentPlaylistIndex < (int)m_playlists.size())
    {
        auto& tracks = m_playlists[m_currentPlaylistIndex].tracks;
        double totalDur = 0;
        for (auto& t : tracks) totalDur += t.durationSec;
        int totalHrs = (int)(totalDur / 3600.0);
        int totalMin = ((int)(totalDur / 60.0)) % 60;

        juce::String countText = juce::String((int)tracks.size()) + " pistes | "
                                  + (totalHrs > 0 ? juce::String(totalHrs) + "h " : "")
                                  + juce::String(totalMin) + "min";
        float badgeX = (float)(centerX + 78);
        float badgeY = (float)(contentTop + 3);
        float badgeW = (float)g.getCurrentFont().getStringWidthFloat(countText) + 16.0f;
        ProDraw::badge(g, countText, badgeX, badgeY, badgeW, 18.0f, Colors::accent());
    }

    if (m_playlists.empty())
    {
        g.setFont(juce::Font("Segoe UI", 12.0f, juce::Font::plain));
        g.setColour(Colors::textMuted());
        g.drawText("Aucune playlist. Cliquez 'Nouvelle' pour creer.", plListX + 8, contentTop + 40,
                   playlistListW - 16, 16, juce::Justification::centredLeft);
    }

    int colY = contentTop + 22;
    int cx = centerX + 8;
    int colHeaderH = 18;

    g.setColour(Colors::bgCard().withAlpha(0.3f));
    g.fillRoundedRectangle((float)cx - 2.0f, (float)colY - 2.0f, (float)(centerW - 12), (float)(colHeaderH + 2), 3.0f);
    g.setColour(Colors::primary().withAlpha(0.2f));
    g.fillRect((float)cx, (float)(colY + colHeaderH - 1), (float)(centerW - 16), 1.0f);

    g.setFont(juce::Font("Segoe UI", 11.0f, juce::Font::bold));
    g.setColour(Colors::textSecondary());

    auto drawColHeader = [&](const juce::String& text, int x, int y, int w, SortColumn col, juce::Justification just)
    {
        if (m_sortColumn == col)
        {
            g.setColour(Colors::primary().withAlpha(0.15f));
            g.fillRoundedRectangle((float)x - 2.0f, (float)y - 1.0f, (float)w + 4.0f, (float)colHeaderH, 2.0f);
            g.setColour(Colors::primary());
        }
        else
        {
            g.setColour(Colors::textSecondary());
        }
        g.drawText(text, x, y, w, 14, just);

        if (m_sortColumn == col)
        {
            juce::String arrow = m_sortAscending ? juce::CharPointer_UTF8("\xe2\x96\xb2") : juce::CharPointer_UTF8("\xe2\x96\xbc");
            g.setFont(juce::Font("Segoe UI", 8.0f, juce::Font::plain));
            g.drawText(arrow, x + w - 12, y, 12, 14, juce::Justification::centredRight);
            g.setFont(juce::Font("Segoe UI", 11.0f, juce::Font::bold));
        }
    };

    drawColHeader("#",       cx,       colY, 30,  SortColumn::Number,   juce::Justification::centredLeft);
    drawColHeader("TITRE",   cx + 32,  colY, 200, SortColumn::Title,    juce::Justification::centredLeft);
    drawColHeader("ARTISTE", cx + 236, colY, 150, SortColumn::Artist,   juce::Justification::centredLeft);
    drawColHeader("BPM",     cx + 390, colY, 50,  SortColumn::BPM,      juce::Justification::centred);
    drawColHeader("KEY",     cx + 444, colY, 40,  SortColumn::Key,      juce::Justification::centred);
    drawColHeader("ENERGY",  cx + 488, colY, 60,  SortColumn::Energy,   juce::Justification::centred);
    drawColHeader("DUREE",   cx + 556, colY, 50,  SortColumn::Duration, juce::Justification::centred);
    drawColHeader("SCORE",   cx + 610, colY, 50,  SortColumn::Score,    juce::Justification::centred);

    ProDraw::sectionHeader(g, "EXPORT:", plListX + 8, getHeight() - bottomH + 2, Colors::accent());

    ProDraw::separator(g, (float)margin, (float)(getHeight() - bottomH - 6), (float)(W - margin * 2));

    ProDraw::separator(g, (float)(centerX - 4), (float)(contentTop + 24), 1.0f);
    ProDraw::separator(g, (float)(scoreX - 4), (float)(contentTop + 24), 1.0f);

    if (m_smartRuleEditor->isVisible() || m_iaBuildDialog->isVisible())
    {
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRect(getLocalBounds());
    }
}

void PlaylistPreparationView::resized()
{
    int margin = 12;
    int headerH = 56;
    int W = getWidth();
    int H = getHeight();

    int libBrowserW = (W - margin * 5) * 25 / 100;  // 25% for library browser
    int playlistListW = 180;
    int rightW = 260;
    int bottomH = 48;
    int contentTop = headerH + 4;
    int contentH = H - contentTop - bottomH - 8;

    m_titleLabel->setBounds(margin + libBrowserW + margin, 6, 350, 36);
    m_titleLabel->setFont(juce::Font("Segoe UI", 22.0f, juce::Font::bold));

    int bx = margin + libBrowserW + margin + 260;
    int bh = 26;
    m_newBtn->setBounds(bx, 16, 80, bh); bx += 84;
    m_openBtn->setBounds(bx, 16, 65, bh); bx += 69;
    m_saveBtn->setBounds(bx, 16, 95, bh); bx += 99;
    m_deleteBtn->setBounds(bx, 16, 80, bh); bx += 84;
    m_duplicateBtn->setBounds(bx, 16, 80, bh); bx += 84;
    m_iaBuildBtn->setBounds(bx, 16, 110, bh); bx += 114;
    m_smartPlaylistBtn->setBounds(bx, 16, 115, bh); bx += 119;
    if (m_addTracksBtn)    { m_addTracksBtn->setBounds(bx, 16, 90, bh); bx += 94; }
    if (m_removeTracksBtn) { m_removeTracksBtn->setBounds(bx, 16, 85, bh); }

    if (m_libraryBrowser)
        m_libraryBrowser->setBounds(margin, 8, libBrowserW, H - 16);

    if (m_filterLabel) m_filterLabel->setVisible(false);
    if (m_filterSearchEdit) m_filterSearchEdit->setVisible(false);
    if (m_filterBpmMin) m_filterBpmMin->setVisible(false);
    if (m_filterBpmMax) m_filterBpmMax->setVisible(false);
    if (m_filterBpmLabel) m_filterBpmLabel->setVisible(false);
    if (m_filterKeyCombo) m_filterKeyCombo->setVisible(false);
    if (m_filterGenreCombo) m_filterGenreCombo->setVisible(false);
    if (m_filterEnergyMin) m_filterEnergyMin->setVisible(false);
    if (m_filterEnergyMax) m_filterEnergyMax->setVisible(false);
    if (m_filterEnergyLabel) m_filterEnergyLabel->setVisible(false);
    if (m_filterRatingCombo) m_filterRatingCombo->setVisible(false);
    if (m_filterResetBtn) m_filterResetBtn->setVisible(false);

    int plListX = margin + libBrowserW + margin;
    m_playlistList->setBounds(plListX + 4, contentTop + 22, playlistListW - 8, contentH - 26);

    int centerX = plListX + playlistListW + 8;
    int centerW = W - (plListX + playlistListW) - rightW - margin * 2 - 8;
    m_trackList->setBounds(centerX + 4, contentTop + 22, centerW - 8, contentH - 26);

    int rx = W - rightW - margin + 8;
    int ry = contentTop + 8;
    int rw = rightW - 16;

    m_autoSortCombo->setBounds(rx, ry, rw - 88, 28);
    m_autoSortBtn->setBounds(rx + rw - 84, ry, 84, 28);
    ry += 34;
    m_fillGapsBtn->setBounds(rx, ry, rw, 28);
    ry += 34;
    if (m_suggestBtn) { m_suggestBtn->setBounds(rx, ry, rw, 28); ry += 32; }
    if (m_fillBtn)    { m_fillBtn->setBounds(rx, ry, rw, 28);    ry += 32; }
    if (m_sendDJBtn)  { m_sendDJBtn->setBounds(rx, ry, rw, 28);  ry += 36; }
    m_scoreDisplay->setBounds(rx, ry, rw, juce::jmax(80, juce::jmin(240, contentH - (ry - contentTop) - 8)));

    int by = H - bottomH + 8;
    int bbx = plListX + 80;
    int bw = 64;
    int btnH = 30;
    m_exportM3UBtn->setBounds(bbx, by, bw, btnH);
    m_exportPLSBtn->setBounds(bbx + (bw + 6),     by, bw, btnH);
    if (m_exportXSPFBtn) m_exportXSPFBtn->setBounds(bbx + (bw + 6) * 2, by, bw, btnH);
    m_exportPDFBtn->setBounds(bbx + (bw + 6) * 3, by, bw, btnH);
    m_exportJSONBtn->setBounds(bbx + (bw + 6) * 4, by, bw, btnH);

    if (m_smartRuleEditor && m_smartRuleEditor->isVisible())
    {
        int dw = 440, dh = 300;
        m_smartRuleEditor->setBounds((W - dw) / 2, (H - dh) / 2, dw, dh);
    }
    if (m_iaBuildDialog && m_iaBuildDialog->isVisible())
    {
        int dw = 340, dh = 170;
        m_iaBuildDialog->setBounds((W - dw) / 2, (H - dh) / 2, dw, dh);
    }
    if (m_nestedSmartEditor && m_nestedSmartEditor->isVisible())
    {
        int dw = std::min(760, W - 40);
        int dh = std::min(520, H - 60);
        m_nestedSmartEditor->setBounds((W - dw) / 2, (H - dh) / 2, dw, dh);
    }
}

void PlaylistPreparationView::sortByColumn(SortColumn col)
{
    if (m_currentPlaylistIndex < 0 || m_currentPlaylistIndex >= (int)m_playlists.size())
        return;

    if (m_sortColumn == col)
        m_sortAscending = !m_sortAscending;
    else
    {
        m_sortColumn = col;
        m_sortAscending = true;
    }

    auto& tracks = m_playlists[m_currentPlaylistIndex].tracks;
    std::stable_sort(tracks.begin(), tracks.end(),
        [this](const TrackInfo& a, const TrackInfo& b) -> bool {
            int cmp = 0;
            switch (m_sortColumn)
            {
            case SortColumn::Number:
                return false; // natural order
            case SortColumn::Title:
                cmp = a.title.compareIgnoreCase(b.title);
                break;
            case SortColumn::Artist:
                cmp = a.artist.compareIgnoreCase(b.artist);
                break;
            case SortColumn::BPM:
                cmp = (a.bpm < b.bpm) ? -1 : (a.bpm > b.bpm ? 1 : 0);
                break;
            case SortColumn::Key:
                cmp = a.key.compareIgnoreCase(b.key);
                break;
            case SortColumn::Energy:
                cmp = (a.energy < b.energy) ? -1 : (a.energy > b.energy ? 1 : 0);
                break;
            case SortColumn::Duration:
                cmp = (a.durationSec < b.durationSec) ? -1 : (a.durationSec > b.durationSec ? 1 : 0);
                break;
            case SortColumn::Score:
                cmp = (a.rating < b.rating) ? -1 : (a.rating > b.rating ? 1 : 0);
                break;
            default:
                break;
            }
            return m_sortAscending ? (cmp < 0) : (cmp > 0);
        });

    m_trackList->updateContent();
    updateCompatibilityIndicators();
    repaint();
}

void PlaylistPreparationView::handleColumnHeaderClick(int columnIndex)
{
    if (columnIndex >= 0 && columnIndex < static_cast<int>(SortColumn::Count))
    {
        spdlog::info("[PlaylistPrep] Sort by column {}", columnIndex);
        sortByColumn(static_cast<SortColumn>(columnIndex));
    }
}

void PlaylistPreparationView::mouseDown(const juce::MouseEvent& e)
{
    // Column-header hit test - MUST mirror paint()/resized() layout exactly.
    int headerH = 56;
    int margin = 12;
    int W = getWidth();
    int libBrowserW = (W - margin * 5) * 25 / 100;
    int playlistListW = 180;
    int plListX = margin + libBrowserW + margin;
    int contentTop = headerH + 4;
    int centerX = plListX + playlistListW + 8;
    int colY = contentTop + 22;
    int colH = 14;

    if (e.y >= colY && e.y < colY + colH && e.x >= centerX)
    {
        static const int colWidths[] = { 32, 204, 154, 54, 44, 68, 54, 50 };
        static const int kNumCols = 8;
        int cx = centerX + 8;
        for (int i = 0; i < kNumCols; ++i)
        {
            if (e.x >= cx && e.x < cx + colWidths[i])
            {
                handleColumnHeaderClick(i);
                return;
            }
            cx += colWidths[i];
        }
    }
}

} // namespace BeatMate::UI
