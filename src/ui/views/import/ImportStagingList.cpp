#include "ImportStagingList.h"
#include "../../styles/ColorPalette.h"
#include "../../../services/config/I18n.h"

namespace BeatMate::UI {

namespace {
constexpr int kHeaderH = 24;
constexpr int kRowH = 48;

juce::String formatDuration(double seconds)
{
    if (seconds <= 0.0) return "-";
    const int mins = static_cast<int>(seconds / 60.0);
    const int secs = static_cast<int>(seconds) % 60;
    return juce::String::formatted("%d:%02d", mins, secs);
}

juce::Colour extColour(const juce::String& ext)
{
    const juce::String e = ext.toUpperCase();
    if (e == "MP3" || e == "AAC" || e == "OGG" || e == "M4A" || e == "WMA" || e == "OPUS")
        return Colors::primary();
    if (e == "WAV" || e == "FLAC" || e == "AIFF" || e == "AIF")
        return Colors::success();
    return Colors::secondary();
}
}

ImportStagingList::ImportStagingList(std::vector<StagedFile>& entries)
    : m_entries(entries)
{
    m_model = std::make_unique<Model>(*this);
    m_listBox = std::make_unique<juce::ListBox>("staging", m_model.get());
    m_listBox->setRowHeight(kRowH);
    m_listBox->setColour(juce::ListBox::backgroundColourId, Colors::bgDarker());
    m_listBox->setColour(juce::ListBox::outlineColourId, Colors::border());
    m_listBox->setOutlineThickness(1);
    addAndMakeVisible(*m_listBox);
}

void ImportStagingList::refresh()
{
    m_listBox->updateContent();
    m_listBox->repaint();
}

void ImportStagingList::refreshRow(int index)
{
    m_listBox->repaintRow(index);
}

int ImportStagingList::selectedRow() const
{
    return m_listBox->getSelectedRow();
}

void ImportStagingList::selectRow(int index)
{
    m_listBox->selectRow(index);
}

void ImportStagingList::retranslateUi()
{
    repaint();
}

void ImportStagingList::paint(juce::Graphics& g)
{
    const int w = getWidth();
    g.setColour(Colors::bgSurface());
    g.fillRect(0, 0, w, kHeaderH);
    g.setColour(Colors::border().withAlpha(0.5f));
    g.fillRect(0, kHeaderH - 1, w, 1);

    g.setFont(juce::Font("Segoe UI", 10.0f, juce::Font::bold));
    g.setColour(Colors::textSecondary());

    const int flexW = w - m_cols.check - m_cols.cover - m_cols.duration
                      - m_cols.format - m_cols.status - m_cols.remove;
    const int titleW = flexW * 40 / 100;
    const int artistW = flexW * 32 / 100;
    const int albumW = flexW - titleW - artistW;

    int x = m_cols.check + m_cols.cover;
    g.drawText(BM_TJ("import.col.title"), x + 4, 0, titleW - 8, kHeaderH, juce::Justification::centredLeft);
    x += titleW;
    g.drawText(BM_TJ("import.col.artist"), x + 4, 0, artistW - 8, kHeaderH, juce::Justification::centredLeft);
    x += artistW;
    g.drawText(BM_TJ("import.col.album"), x + 4, 0, albumW - 8, kHeaderH, juce::Justification::centredLeft);
    x += albumW;
    g.drawText(BM_TJ("import.col.duration"), x, 0, m_cols.duration, kHeaderH, juce::Justification::centred);
    x += m_cols.duration;
    g.drawText(BM_TJ("import.col.format"), x, 0, m_cols.format, kHeaderH, juce::Justification::centred);
    x += m_cols.format;
    g.drawText(BM_TJ("import.col.status"), x, 0, m_cols.status, kHeaderH, juce::Justification::centred);
}

void ImportStagingList::resized()
{
    m_listBox->setBounds(0, kHeaderH, getWidth(), getHeight() - kHeaderH);
}

int ImportStagingList::Model::getNumRows()
{
    return static_cast<int>(m_owner.m_entries.size());
}

void ImportStagingList::Model::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= static_cast<int>(m_owner.m_entries.size()))
        return;
    const auto& e = m_owner.m_entries[static_cast<size_t>(row)];
    const auto& cols = m_owner.m_cols;

    if (selected)
    {
        g.setColour(Colors::primary().withAlpha(0.16f));
        g.fillRect(0, 0, w, h);
        g.setColour(Colors::primary());
        g.fillRect(0, 2, 3, h - 4);
    }
    else if (row % 2 == 1)
    {
        g.setColour(juce::Colour(0x05FFFFFF));
        g.fillRect(0, 0, w, h);
    }

    const float alpha = e.selected ? 1.0f : 0.4f;

    {
        juce::Rectangle<float> box(8.0f, h / 2.0f - 8.0f, 16.0f, 16.0f);
        g.setColour(Colors::border());
        g.drawRoundedRectangle(box, 4.0f, 1.2f);
        if (e.selected)
        {
            g.setColour(Colors::primary());
            g.fillRoundedRectangle(box.reduced(3.0f), 2.5f);
        }
    }

    {
        juce::Rectangle<float> coverBox(static_cast<float>(cols.check) + 2.0f,
                                        h / 2.0f - 19.0f, 38.0f, 38.0f);
        if (e.cover.isValid())
        {
            g.drawImage(e.cover, coverBox, juce::RectanglePlacement::fillDestination);
            g.setColour(Colors::border().withAlpha(0.6f));
            g.drawRoundedRectangle(coverBox, 3.0f, 0.8f);
        }
        else
        {
            g.setColour(Colors::bgSurface());
            g.fillRoundedRectangle(coverBox, 3.0f);
            g.setColour(Colors::textDim().withAlpha(alpha));
            g.setFont(juce::Font(15.0f));
            g.drawText(juce::CharPointer_UTF8("\xe2\x99\xaa"), coverBox, juce::Justification::centred);
        }
    }

    const int flexW = w - cols.check - cols.cover - cols.duration
                      - cols.format - cols.status - cols.remove;
    const int titleW = flexW * 40 / 100;
    const int artistW = flexW * 32 / 100;
    const int albumW = flexW - titleW - artistW;

    int x = cols.check + cols.cover;
    g.setColour(Colors::textPrimary().withAlpha(alpha));
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText(e.displayTitle(), x + 4, 6, titleW - 8, 16, juce::Justification::centredLeft, true);
    g.setColour(Colors::textDim().withAlpha(alpha));
    g.setFont(juce::Font(9.5f));
    g.drawText(e.path, x + 4, 24, titleW - 8, 12, juce::Justification::centredLeft, true);
    x += titleW;

    g.setColour(Colors::textSecondary().withAlpha(alpha));
    g.setFont(juce::Font(11.5f));
    g.drawText(e.displayArtist(), x + 4, 0, artistW - 8, h, juce::Justification::centredLeft, true);
    x += artistW;
    g.drawText(e.displayAlbum(), x + 4, 0, albumW - 8, h, juce::Justification::centredLeft, true);
    x += albumW;

    g.setFont(juce::Font("Consolas", 11.0f, juce::Font::plain));
    g.setColour(Colors::textSecondary().withAlpha(alpha));
    g.drawText(formatDuration(e.meta.duration), x, 0, cols.duration, h, juce::Justification::centred);
    x += cols.duration;

    ProDraw::badge(g, e.ext.toUpperCase(), static_cast<float>(x) + 3.0f, h / 2.0f - 9.0f,
                   static_cast<float>(cols.format) - 6.0f, 18.0f, extColour(e.ext).withAlpha(alpha));
    x += cols.format;

    {
        juce::String label;
        juce::Colour col = Colors::textDim();
        switch (e.dupState)
        {
            case StagedFile::DupState::ExactDuplicate:
                label = BM_TJ("import.status.exactDuplicate");
                col = Colors::warning();
                break;
            case StagedFile::DupState::ProbableDuplicate:
                label = BM_TJ("import.status.probableDuplicate")
                        + " " + juce::String(static_cast<int>(e.dupConfidence * 100.0f)) + "%";
                col = juce::Colour(0xFFF97316);
                break;
            default:
                if (e.metaState == StagedFile::MetaState::Failed)
                {
                    label = BM_TJ("import.status.metaError");
                    col = Colors::error();
                }
                else if (e.metaState == StagedFile::MetaState::Pending)
                {
                    label = "...";
                    col = Colors::textDim();
                }
                else
                {
                    label = BM_TJ("import.status.ok");
                    col = Colors::success();
                }
                break;
        }
        const float bw = juce::jmin(static_cast<float>(cols.status) - 8.0f, 138.0f);
        ProDraw::badge(g, label, static_cast<float>(x) + (cols.status - bw) / 2.0f,
                       h / 2.0f - 9.0f, bw, 18.0f, col.withAlpha(alpha));
    }
    x += cols.status;

    g.setColour(Colors::error().withAlpha(0.75f * alpha));
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText("x", x, 0, cols.remove - 6, h, juce::Justification::centred);

    g.setColour(Colors::border().withAlpha(0.15f));
    g.drawHorizontalLine(h - 1, 4.0f, static_cast<float>(w - 4));
}

void ImportStagingList::Model::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    const auto& cols = m_owner.m_cols;
    const int x = e.getPosition().x;
    if (x < cols.check + 4)
    {
        if (m_owner.onToggleRow) m_owner.onToggleRow(row);
        return;
    }
    if (x > m_owner.getWidth() - cols.remove)
    {
        if (m_owner.onRemoveRow) m_owner.onRemoveRow(row);
        return;
    }
}

void ImportStagingList::Model::selectedRowsChanged(int lastRowSelected)
{
    if (m_owner.onRowSelected) m_owner.onRowSelected(lastRowSelected);
}

} // namespace BeatMate::UI
