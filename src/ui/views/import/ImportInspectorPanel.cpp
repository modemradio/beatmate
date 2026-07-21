#include "ImportInspectorPanel.h"
#include "../../styles/ColorPalette.h"
#include "../../../services/config/I18n.h"

namespace BeatMate::UI {

namespace {
constexpr int kMargin = 12;
constexpr int kCoverH = 170;
constexpr int kFieldTitle = 0;
constexpr int kFieldArtist = 1;
constexpr int kFieldAlbum = 2;
constexpr int kFieldGenre = 3;
}

ImportInspectorPanel::ImportInspectorPanel()
{
    auto makeLabel = [this](const char* key) {
        auto l = std::make_unique<juce::Label>("l", BM_TJ(key));
        l->setFont(juce::Font("Segoe UI", 10.0f, juce::Font::bold));
        l->setColour(juce::Label::textColourId, Colors::textSecondary());
        addAndMakeVisible(*l);
        return l;
    };
    auto makeEditor = [this](int field) {
        auto e = std::make_unique<juce::TextEditor>();
        e->setFont(juce::Font(12.5f));
        e->setColour(juce::TextEditor::backgroundColourId, Colors::bgSurface());
        e->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
        e->setColour(juce::TextEditor::outlineColourId, Colors::border());
        e->setColour(juce::TextEditor::focusedOutlineColourId, Colors::primary());
        auto* raw = e.get();
        e->onReturnKey = [this, field, raw] { commitField(field, *raw); };
        e->onFocusLost = [this, field, raw] { commitField(field, *raw); };
        addAndMakeVisible(*e);
        return e;
    };

    m_titleLabel = makeLabel("import.inspector.fieldTitle");
    m_titleEd = makeEditor(kFieldTitle);
    m_artistLabel = makeLabel("import.inspector.fieldArtist");
    m_artistEd = makeEditor(kFieldArtist);
    m_albumLabel = makeLabel("import.inspector.fieldAlbum");
    m_albumEd = makeEditor(kFieldAlbum);
    m_genreLabel = makeLabel("import.inspector.fieldGenre");
    m_genreEd = makeEditor(kFieldGenre);
}

void ImportInspectorPanel::commitField(int field, juce::TextEditor& editor)
{
    if (!m_hasEntry || m_index < 0)
        return;
    if (onFieldEdited)
        onFieldEdited(m_index, field, editor.getText());
}

void ImportInspectorPanel::setEntry(const StagedFile* entry, int index)
{
    if (!entry)
    {
        clearEntry();
        return;
    }
    m_index = index;
    m_hasEntry = true;
    m_snapshot = *entry;
    m_titleEd->setText(entry->displayTitle(), juce::dontSendNotification);
    m_artistEd->setText(entry->displayArtist(), juce::dontSendNotification);
    m_albumEd->setText(entry->displayAlbum(), juce::dontSendNotification);
    m_genreEd->setText(entry->displayGenre(), juce::dontSendNotification);
    const bool enabled = true;
    m_titleEd->setEnabled(enabled);
    m_artistEd->setEnabled(enabled);
    m_albumEd->setEnabled(enabled);
    m_genreEd->setEnabled(enabled);
    repaint();
}

void ImportInspectorPanel::clearEntry()
{
    m_index = -1;
    m_hasEntry = false;
    m_snapshot = StagedFile{};
    for (auto* e : { m_titleEd.get(), m_artistEd.get(), m_albumEd.get(), m_genreEd.get() })
    {
        e->setText({}, juce::dontSendNotification);
        e->setEnabled(false);
    }
    repaint();
}

juce::String ImportInspectorPanel::infoLine() const
{
    juce::StringArray parts;
    if (m_snapshot.meta.duration > 0.0)
    {
        const int mins = static_cast<int>(m_snapshot.meta.duration / 60.0);
        const int secs = static_cast<int>(m_snapshot.meta.duration) % 60;
        parts.add(juce::String::formatted("%d:%02d", mins, secs));
    }
    if (m_snapshot.ext.isNotEmpty())
        parts.add(m_snapshot.ext.toUpperCase());
    if (m_snapshot.meta.sampleRate > 0)
        parts.add(juce::String(m_snapshot.meta.sampleRate / 1000.0, 1) + " kHz");
    if (m_snapshot.meta.fileSize > 0)
        parts.add(juce::String(m_snapshot.meta.fileSize / (1024.0 * 1024.0), 1) + " Mo");
    return parts.joinIntoString("  \xc2\xb7  ");
}

void ImportInspectorPanel::paint(juce::Graphics& g)
{
    ProDraw::glassPanel(g, getLocalBounds().toFloat(), 10.0f);

    if (!m_hasEntry)
    {
        ProDraw::emptyState(g, getLocalBounds(),
                            juce::CharPointer_UTF8("\xe2\x84\xb9"),
                            BM_TJ("import.inspector.emptyTitle"),
                            BM_TJ("import.inspector.emptyBody"),
                            Colors::primary());
        return;
    }

    const int w = getWidth();
    juce::Rectangle<float> coverBox(static_cast<float>(kMargin), static_cast<float>(kMargin),
                                    static_cast<float>(w - kMargin * 2), static_cast<float>(kCoverH));
    if (m_snapshot.cover.isValid())
    {
        g.drawImage(m_snapshot.cover, coverBox, juce::RectanglePlacement::centred);
    }
    else
    {
        g.setColour(Colors::bgSurface());
        g.fillRoundedRectangle(coverBox, 6.0f);
        g.setColour(Colors::textDim());
        g.setFont(juce::Font(44.0f));
        g.drawText(juce::CharPointer_UTF8("\xe2\x99\xaa"), coverBox, juce::Justification::centred);
    }
    g.setColour(Colors::border().withAlpha(0.5f));
    g.drawRoundedRectangle(coverBox, 6.0f, 1.0f);

    g.setFont(juce::Font("Consolas", 10.5f, juce::Font::plain));
    g.setColour(Colors::textSecondary());
    g.drawText(infoLine(), kMargin, kMargin + kCoverH + 4, w - kMargin * 2, 16,
               juce::Justification::centred, true);

    const int fieldsBottom = kMargin + kCoverH + 26 + 4 * 46;
    if (m_snapshot.dupState == StagedFile::DupState::ExactDuplicate
        || m_snapshot.dupState == StagedFile::DupState::ProbableDuplicate)
    {
        juce::Rectangle<float> warnBox(static_cast<float>(kMargin), static_cast<float>(fieldsBottom + 6),
                                       static_cast<float>(w - kMargin * 2), 46.0f);
        g.setColour(Colors::warning().withAlpha(0.12f));
        g.fillRoundedRectangle(warnBox, 6.0f);
        g.setColour(Colors::warning().withAlpha(0.5f));
        g.drawRoundedRectangle(warnBox, 6.0f, 1.0f);
        g.setColour(Colors::warning());
        g.setFont(juce::Font("Segoe UI", 10.5f, juce::Font::bold));
        juce::String head = m_snapshot.dupState == StagedFile::DupState::ExactDuplicate
            ? BM_TJ("import.status.exactDuplicate")
            : BM_TJ("import.inspector.resembles") + " "
              + juce::String(static_cast<int>(m_snapshot.dupConfidence * 100.0f)) + "%";
        g.drawText(head, static_cast<int>(warnBox.getX()) + 8, static_cast<int>(warnBox.getY()) + 5,
                   static_cast<int>(warnBox.getWidth()) - 16, 15, juce::Justification::centredLeft, true);
        g.setColour(Colors::textSecondary());
        g.setFont(juce::Font(10.5f));
        g.drawText(m_snapshot.dupMatchLabel, static_cast<int>(warnBox.getX()) + 8,
                   static_cast<int>(warnBox.getY()) + 23, static_cast<int>(warnBox.getWidth()) - 16, 15,
                   juce::Justification::centredLeft, true);
    }

    g.setColour(Colors::textDim());
    g.setFont(juce::Font(9.0f));
    g.drawText(m_snapshot.path, kMargin, getHeight() - 20, w - kMargin * 2, 14,
               juce::Justification::centredLeft, true);
}

void ImportInspectorPanel::resized()
{
    const int w = getWidth();
    int y = kMargin + kCoverH + 26;
    auto place = [&](juce::Label& l, juce::TextEditor& e) {
        l.setBounds(kMargin, y, w - kMargin * 2, 14);
        e.setBounds(kMargin, y + 15, w - kMargin * 2, 26);
        y += 46;
    };
    place(*m_titleLabel, *m_titleEd);
    place(*m_artistLabel, *m_artistEd);
    place(*m_albumLabel, *m_albumEd);
    place(*m_genreLabel, *m_genreEd);
}

void ImportInspectorPanel::retranslateUi()
{
    m_titleLabel->setText(BM_TJ("import.inspector.fieldTitle"), juce::dontSendNotification);
    m_artistLabel->setText(BM_TJ("import.inspector.fieldArtist"), juce::dontSendNotification);
    m_albumLabel->setText(BM_TJ("import.inspector.fieldAlbum"), juce::dontSendNotification);
    m_genreLabel->setText(BM_TJ("import.inspector.fieldGenre"), juce::dontSendNotification);
    repaint();
}

} // namespace BeatMate::UI
