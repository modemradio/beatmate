#include "DJPlaylistPicker.h"
#include <algorithm>
#include "../../styles/ColorPalette.h"
#include "../../../services/config/I18n.h"

namespace BeatMate::UI {

namespace {
constexpr int kControlsH = 60;

int depthOf(const std::string& parentPath)
{
    if (parentPath.empty())
        return 0;
    return 1 + static_cast<int>(std::count(parentPath.begin(), parentPath.end(), '/'));
}
}

DJPlaylistPicker::DJPlaylistPicker()
{
    m_wholeCollectionCheck = std::make_unique<juce::ToggleButton>(BM_TJ("import.dj.wholeCollection"));
    m_wholeCollectionCheck->setColour(juce::ToggleButton::textColourId, Colors::textPrimary());
    m_wholeCollectionCheck->onClick = [this] {
        const bool whole = m_wholeCollectionCheck->getToggleState();
        m_listBox->setEnabled(!whole);
        m_listBox->setAlpha(whole ? 0.4f : 1.0f);
        notifySelection();
    };
    addAndMakeVisible(*m_wholeCollectionCheck);

    m_searchEditor = std::make_unique<juce::TextEditor>();
    m_searchEditor->setTextToShowWhenEmpty(BM_TJ("import.dj.search"), Colors::textMuted());
    m_searchEditor->setFont(juce::Font(12.0f));
    m_searchEditor->setColour(juce::TextEditor::backgroundColourId, Colors::bgSurface());
    m_searchEditor->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
    m_searchEditor->setColour(juce::TextEditor::outlineColourId, Colors::border());
    m_searchEditor->onTextChange = [this] { rebuildFiltered(); };
    addAndMakeVisible(*m_searchEditor);

    m_model = std::make_unique<Model>(*this);
    m_listBox = std::make_unique<juce::ListBox>("djpl", m_model.get());
    m_listBox->setRowHeight(28);
    m_listBox->setColour(juce::ListBox::backgroundColourId, Colors::bgDarker());
    m_listBox->setColour(juce::ListBox::outlineColourId, Colors::border());
    m_listBox->setOutlineThickness(1);
    addAndMakeVisible(*m_listBox);
}

void DJPlaylistPicker::setPlaylists(std::vector<Services::DJSoftware::ExternalPlaylistDescriptor> playlists)
{
    m_all = std::move(playlists);
    m_checked.clear();
    m_wholeCollectionCheck->setToggleState(false, juce::dontSendNotification);
    m_listBox->setEnabled(true);
    m_listBox->setAlpha(1.0f);
    rebuildFiltered();
    notifySelection();
}

void DJPlaylistPicker::rebuildFiltered()
{
    m_filtered.clear();
    const juce::String query = m_searchEditor->getText();
    for (int i = 0; i < static_cast<int>(m_all.size()); ++i)
    {
        const auto& pl = m_all[static_cast<size_t>(i)];
        if (pl.isFolder)
            continue;
        if (query.isNotEmpty()
            && !juce::String::fromUTF8(pl.name.c_str()).containsIgnoreCase(query))
            continue;
        m_filtered.push_back(i);
    }
    m_listBox->updateContent();
    m_listBox->repaint();
}

void DJPlaylistPicker::notifySelection()
{
    repaint();
    if (onSelectionChanged)
        onSelectionChanged();
}

std::vector<std::string> DJPlaylistPicker::selectedIds() const
{
    return { m_checked.begin(), m_checked.end() };
}

bool DJPlaylistPicker::wholeCollection() const
{
    return m_wholeCollectionCheck->getToggleState();
}

int DJPlaylistPicker::selectedTrackCount() const
{
    int total = 0;
    for (const auto& pl : m_all)
        if (m_checked.count(pl.externalId) > 0)
            total += pl.trackCount;
    return total;
}

void DJPlaylistPicker::paint(juce::Graphics& g)
{
    g.setFont(juce::Font("Segoe UI", 11.0f, juce::Font::bold));
    g.setColour(Colors::textSecondary());
    g.drawText(BM_TJ("import.dj.playlistsHeader"), 0, 0, getWidth(), 16,
               juce::Justification::centredLeft);

    if (!m_wholeCollectionCheck->getToggleState())
    {
        juce::String fmt = BM_TJ("import.dj.selectionFmt");
        const juce::String txt = fmt.replace("{playlists}", juce::String(m_checked.size()))
                                     .replace("{tracks}", juce::String(selectedTrackCount()));
        g.setFont(juce::Font(10.5f));
        g.setColour(Colors::textMuted());
        g.drawText(txt, 0, getHeight() - 18, getWidth(), 16, juce::Justification::centredLeft);
    }
}

void DJPlaylistPicker::resized()
{
    const int w = getWidth();
    m_wholeCollectionCheck->setBounds(0, 18, w / 2, 22);
    m_searchEditor->setBounds(w / 2 + 8, 18, w / 2 - 8, 22);
    m_listBox->setBounds(0, kControlsH - 14, w, getHeight() - kControlsH - 6);
}

void DJPlaylistPicker::retranslateUi()
{
    m_wholeCollectionCheck->setButtonText(BM_TJ("import.dj.wholeCollection"));
    m_searchEditor->setTextToShowWhenEmpty(BM_TJ("import.dj.search"), Colors::textMuted());
    repaint();
}

int DJPlaylistPicker::Model::getNumRows()
{
    return static_cast<int>(m_owner.m_filtered.size());
}

void DJPlaylistPicker::Model::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool)
{
    if (row < 0 || row >= static_cast<int>(m_owner.m_filtered.size()))
        return;
    const auto& pl = m_owner.m_all[static_cast<size_t>(m_owner.m_filtered[static_cast<size_t>(row)])];
    const bool checked = m_owner.m_checked.count(pl.externalId) > 0;

    if (checked)
    {
        g.setColour(Colors::primary().withAlpha(0.10f));
        g.fillRect(0, 0, w, h);
    }
    else if (row % 2 == 1)
    {
        g.setColour(juce::Colour(0x05FFFFFF));
        g.fillRect(0, 0, w, h);
    }

    juce::Rectangle<float> box(8.0f, h / 2.0f - 7.0f, 14.0f, 14.0f);
    g.setColour(Colors::border());
    g.drawRoundedRectangle(box, 3.5f, 1.1f);
    if (checked)
    {
        g.setColour(Colors::primary());
        g.fillRoundedRectangle(box.reduced(2.5f), 2.0f);
    }

    const int indent = depthOf(pl.parentPath) * 14;
    g.setColour(Colors::textPrimary());
    g.setFont(juce::Font(11.5f, checked ? juce::Font::bold : juce::Font::plain));
    g.drawText(juce::String::fromUTF8(pl.name.c_str()),
               30 + indent, 0, w - 96 - indent, h, juce::Justification::centredLeft, true);

    if (pl.trackCount > 0)
    {
        g.setColour(Colors::textMuted());
        g.setFont(juce::Font("Consolas", 10.0f, juce::Font::plain));
        g.drawText(juce::String(pl.trackCount), w - 60, 0, 52, h, juce::Justification::centredRight);
    }

    g.setColour(Colors::border().withAlpha(0.12f));
    g.drawHorizontalLine(h - 1, 4.0f, static_cast<float>(w - 4));
}

void DJPlaylistPicker::Model::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= static_cast<int>(m_owner.m_filtered.size()))
        return;
    const auto& pl = m_owner.m_all[static_cast<size_t>(m_owner.m_filtered[static_cast<size_t>(row)])];
    if (m_owner.m_checked.count(pl.externalId) > 0)
        m_owner.m_checked.erase(pl.externalId);
    else
        m_owner.m_checked.insert(pl.externalId);
    m_owner.m_listBox->repaintRow(row);
    m_owner.notifySelection();
}

} // namespace BeatMate::UI
