#include "TrackPrepChecklist.h"
#include "../../styles/ColorPalette.h"
namespace BeatMate::UI {

TrackPrepChecklist::TrackPrepChecklist() {
    resetChecklist();
    setSize(200, 180);
}

void TrackPrepChecklist::resetChecklist() {
    m_items.clear();
    m_items.push_back({"BPM Detected",      false, true, "Track BPM has been analyzed"});
    m_items.push_back({"Key Detected",       false, true, "Musical key has been detected"});
    m_items.push_back({"Beat Grid Set",      false, true, "Beat grid aligned correctly"});
    m_items.push_back({"Cue Points Set",     false, false, "At least one cue point placed"});
    m_items.push_back({"Waveform Generated", false, true, "Waveform overview generated"});
    m_items.push_back({"Gain Analyzed",      false, true, "Gain/loudness analysis done"});
    m_items.push_back({"Genre Tagged",       false, false, "Genre metadata present"});
    m_items.push_back({"Listened",           false, false, "Track has been previewed"});
    repaint();
}

void TrackPrepChecklist::setItem(int index, bool checked) {
    if (index >= 0 && index < (int)m_items.size()) { m_items[index].checked = checked; repaint(); }
}

void TrackPrepChecklist::setItemLabel(int index, const juce::String& label) {
    if (index >= 0 && index < (int)m_items.size()) { m_items[index].label = label; repaint(); }
}

bool TrackPrepChecklist::isItemChecked(int index) const {
    return index >= 0 && index < (int)m_items.size() ? m_items[index].checked : false;
}

int TrackPrepChecklist::getCheckedCount() const {
    int c = 0;
    for (auto& item : m_items) if (item.checked) c++;
    return c;
}

float TrackPrepChecklist::getCompletionPercent() const {
    if (m_items.empty()) return 0.0f;
    return (float)getCheckedCount() / (float)m_items.size();
}

bool TrackPrepChecklist::isComplete() const { return getCheckedCount() == (int)m_items.size(); }

void TrackPrepChecklist::setHasBPM(bool has) { setItem(0, has); }
void TrackPrepChecklist::setHasKey(bool has) { setItem(1, has); }
void TrackPrepChecklist::setHasBeatGrid(bool has) { setItem(2, has); }
void TrackPrepChecklist::setHasCuePoints(bool has) { setItem(3, has); }
void TrackPrepChecklist::setHasWaveform(bool has) { setItem(4, has); }
void TrackPrepChecklist::setHasGainAnalysis(bool has) { setItem(5, has); }

void TrackPrepChecklist::paint(juce::Graphics& g) {
    int w = getWidth(), h = getHeight();
    ProDraw::viewBackground(g, getWidth(), getHeight());

    g.setColour(Colors::bgMedium());
    g.fillRect(0, 0, w, 24);
    g.setColour(Colors::textPrimary());
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.drawText("PREPARATION", 8, 0, 120, 24, juce::Justification::centredLeft);

    float pct = getCompletionPercent();
    juce::Colour pctColor = pct >= 1.0f ? Colors::success() : (pct >= 0.5f ? Colors::warning() : Colors::error());
    g.setColour(pctColor);
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.drawText(juce::String((int)(pct * 100)) + "%", w - 50, 0, 46, 24, juce::Justification::centredRight);

    int barY = 24;
    g.setColour(Colors::bgLighter());
    g.fillRect(0, barY, w, 3);
    g.setColour(pctColor);
    g.fillRect(0, barY, (int)(w * pct), 3);

    int itemH = 20;
    int y = 30;
    for (int i = 0; i < (int)m_items.size(); ++i) {
        auto& item = m_items[i];
        if (y + itemH > h) break;

        int cbX = 8, cbY = y + 3, cbSize = 14;
        g.setColour(Colors::bgLighter());
        g.fillRoundedRectangle((float)cbX, (float)cbY, (float)cbSize, (float)cbSize, 3.0f);

        if (item.checked) {
            g.setColour(Colors::success());
            g.fillRoundedRectangle((float)cbX, (float)cbY, (float)cbSize, (float)cbSize, 3.0f);
            juce::Path check;
            check.startNewSubPath(cbX + 3.0f, cbY + cbSize * 0.5f);
            check.lineTo(cbX + cbSize * 0.4f, cbY + cbSize - 3.0f);
            check.lineTo(cbX + cbSize - 3.0f, cbY + 3.0f);
            g.setColour(juce::Colours::white);
            g.strokePath(check, juce::PathStrokeType(2.0f));
        } else {
            g.setColour(Colors::border());
            g.drawRoundedRectangle((float)cbX, (float)cbY, (float)cbSize, (float)cbSize, 3.0f, 1.0f);
        }

        g.setColour(item.checked ? Colors::textSecondary() : Colors::textMuted());
        g.setFont(juce::Font(9.0f));
        g.drawText(item.label, cbX + cbSize + 6, y, w - cbX - cbSize - 12, itemH, juce::Justification::centredLeft);

        if (item.autoCheckable) {
            g.setColour(Colors::textDim());
            g.setFont(juce::Font(7.0f));
            g.drawText("AUTO", w - 32, y, 28, itemH, juce::Justification::centredRight);
        }

        y += itemH;
    }

    g.setColour(Colors::border());
    g.drawRect(getLocalBounds(), 1);
}

void TrackPrepChecklist::mouseDown(const juce::MouseEvent& e) {
    int y = 30, itemH = 20;
    for (int i = 0; i < (int)m_items.size(); ++i) {
        if (e.y >= y && e.y < y + itemH) {
            if (!m_items[i].autoCheckable) {
                m_items[i].checked = !m_items[i].checked;
                repaint();
            }
            break;
        }
        y += itemH;
    }
}

} // namespace BeatMate::UI
