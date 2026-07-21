#include "TagEditorPanel.h"
#include "../../styles/ColorPalette.h"
#include "../../widgets/ToastNotifier.h"
#include "../../../services/library/TrackDataProvider.h"
#include "../../../services/library/TrackMetadata.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace BeatMate::UI {

namespace {

const juce::String kMultiple = "<multiple>";

const char* kFieldNames[] = {
    "Titre", "Artiste", "Album", "Genre", "Ann\xc3\xa9""e",
    "BPM", "Cl\xc3\xa9 (8A, Am\xe2\x80\xa6)", "\xc3\x89nergie (1-10)",
    "Label", "Mood", "Commentaire", "Tags perso (virgules)"
};

juce::String toTitleCase(const juce::String& s)
{
    juce::String out;
    bool newWord = true;
    for (int i = 0; i < s.length(); ++i)
    {
        const juce::juce_wchar c = s[i];
        if (newWord && juce::CharacterFunctions::isLetter(c))
        {
            out << juce::String::charToString(juce::CharacterFunctions::toUpperCase(c));
            newWord = false;
        }
        else
        {
            out << juce::String::charToString(juce::CharacterFunctions::toLowerCase(c));
        }
        if (c == ' ' || c == '-' || c == '(' || c == '[' || c == '.' || c == '/')
            newWord = true;
    }
    return out;
}

juce::String searchClean(juce::String s)
{
    s = s.trim();
    s = s.replaceCharacter('_', ' ');
    while (true)
    {
        auto t = s.trimStart();
        int i = 0;
        while (i < t.length() && i < 3 && juce::CharacterFunctions::isDigit(t[i])) ++i;
        if (i == 0) break;
        auto rest = t.substring(i).trimStart();
        if (rest.startsWithChar('-') || rest.startsWithChar('.') || rest.startsWithChar(')')
            || rest.startsWithChar(']') || rest.startsWithChar('_'))
            s = rest.substring(1).trimStart();
        else break;
    }
    s = s.replace("(1)", " ").replace("(2)", " ").replace("(3)", " ")
         .replace("[1]", " ").replace("[2]", " ");
    static const char* junk[] = {
        "www.", ".com", ".fr", ".net", "zone-telechargement", "torrent",
        "official video", "official audio", "lyric video", "audio only",
        "hd", "hq", "320kbps", "128kbps", "free download", "extended mix edit"
    };
    for (auto* j : junk) s = s.replace(j, " ", true);
    s = s.replaceCharacter('.', ' ');
    while (s.contains("  ")) s = s.replace("  ", " ");
    return s.trim();
}

juce::String searchCore(const juce::String& in)
{
    juce::String s = in;
    static const char* marks[] = { "(", "[", " feat", " ft ", " ft.", " remix", " edit",
                                   " version", " live", " radio", " extended", " mix" };
    for (auto* m : marks)
    {
        const int i = s.indexOfIgnoreCase(m);
        if (i > 6) s = s.substring(0, i);
    }
    return s.trim();
}

juce::StringArray matchTokens(const juce::String& s)
{
    juce::String norm = s.toLowerCase();
    norm = norm.replace("feat.", " ").replace("feat ", " ").replace("ft.", " ")
               .replace("&", " ").replace(",", " ").replace("-", " ")
               .replace("(", " ").replace(")", " ").replace("[", " ").replace("]", " ")
               .replace("'", " ").replace(".", " ").replace("/", " ");
    auto tokens = juce::StringArray::fromTokens(norm, " \t", "");
    tokens.removeEmptyStrings();
    for (int i = tokens.size(); --i >= 0;)
        if (tokens[i] == "the" || tokens[i] == "le" || tokens[i] == "la" || tokens[i] == "les"
            || tokens[i] == "a" || tokens[i] == "de" || tokens[i] == "du")
            tokens.remove(i);
    return tokens;
}

double onlineMatchScore(const juce::String& localArtistTitle, const juce::String& candidateArtistTitle)
{
    auto a = matchTokens(localArtistTitle);
    auto b = matchTokens(candidateArtistTitle);
    if (a.isEmpty() || b.isEmpty()) return 0.0;
    int common = 0;
    for (const auto& t : a)
        if (b.contains(t)) ++common;
    const int uni = a.size() + b.size() - common;
    return uni > 0 ? (double) common / (double) uni : 0.0;
}

// Résultats pièges : versions karaoké/reprise/tribute à rejeter si le morceau
bool isSuspiciousCandidate(const juce::String& local, const juce::String& candidate)
{
    static const char* traps[] = { "karaoke", "karaok\xc3\xa9", "tribute", "cover version",
                                   "made famous", "originally performed", "in the style of",
                                   "instrumental version", "workout", "8-bit", "lullaby",
                                   "ringtone", "music box" };
    const auto lc = candidate.toLowerCase();
    const auto ll = local.toLowerCase();
    for (auto* t : traps)
        if (lc.contains(t) && ! ll.contains(t)) return true;
    return false;
}

juce::String cleanedTagText(juce::String s)
{
    s = s.replaceCharacter('_', ' ');
    static const char* junk[] = {
        "(Official Video)", "(Official Music Video)", "(Official Audio)",
        "(Lyric Video)", "(Lyrics)", "(Audio)", "(HD)", "(HQ)",
        "[Official Video]", "[Official Audio]", "[HD]", "[HQ]",
        "(Clip Officiel)", "(clip officiel)", "(Videoclip)"
    };
    for (auto* j : junk)
        s = s.replace(j, "", true);
    while (s.contains("  ")) s = s.replace("  ", " ");
    return s.trim();
}

}

class TagEditorPanel::BatchProgressComponent : public juce::Component
{
public:
    BatchProgressComponent(const juce::String& title, std::function<void()> onCancel)
        : m_title(title), m_onCancel(std::move(onCancel))
    {
        m_bar = std::make_unique<juce::ProgressBar>(m_progress);
        m_bar->setColour(juce::ProgressBar::foregroundColourId, Colors::primary());
        m_bar->setColour(juce::ProgressBar::backgroundColourId, Colors::bgLighter());
        addAndMakeVisible(*m_bar);

        m_cancelBtn = std::make_unique<juce::TextButton>(juce::String::fromUTF8("Annuler"));
        m_cancelBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
        m_cancelBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        m_cancelBtn->onClick = [this] {
            if (m_onCancel) m_onCancel();
            closeDialog();
        };
        addAndMakeVisible(*m_cancelBtn);
        setSize(500, 150);
    }

    void update(int done, int total, const juce::String& line)
    {
        m_progress = total > 0 ? (double) done / (double) total : 0.0;
        m_count = juce::String(done) + " / " + juce::String(total);
        m_line = line;
        repaint();
    }

    void closeDialog()
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(Colors::bgDark());
        g.setColour(Colors::textPrimary());
        g.setFont(juce::Font(15.0f, juce::Font::bold));
        g.drawText(m_title, 20, 14, getWidth() - 140, 22, juce::Justification::centredLeft, true);
        g.setColour(Colors::primary());
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText(m_count, getWidth() - 120, 14, 100, 22, juce::Justification::centredRight);
        g.setColour(Colors::textSecondary());
        g.setFont(juce::Font(12.5f));
        g.drawText(m_line, 20, 42, getWidth() - 40, 18, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        m_bar->setBounds(20, 70, getWidth() - 40, 22);
        m_cancelBtn->setBounds(getWidth() - 120, getHeight() - 42, 100, 30);
    }

private:
    double m_progress = 0.0;
    juce::String m_title, m_count, m_line;
    std::function<void()> m_onCancel;
    std::unique_ptr<juce::ProgressBar> m_bar;
    std::unique_ptr<juce::TextButton> m_cancelBtn;
};

void TagEditorPanel::openBatchProgress(const juce::String& title,
                                       std::shared_ptr<std::atomic<bool>> cancelFlag)
{
    closeBatchProgress();
    m_batchCancel = cancelFlag;
    auto* comp = new BatchProgressComponent(title,
        [cancelFlag] { if (cancelFlag) cancelFlag->store(true); });
    m_progressPopup = comp;

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(comp);
    opts.dialogTitle = title;
    opts.dialogBackgroundColour = Colors::bgDark();
    opts.escapeKeyTriggersCloseButton = false;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.launchAsync();
}

void TagEditorPanel::updateBatchProgress(int done, int total, const juce::String& line)
{
    if (m_progressPopup != nullptr)
        m_progressPopup->update(done, total, line);
}

void TagEditorPanel::closeBatchProgress()
{
    if (m_progressPopup != nullptr)
        m_progressPopup->closeDialog();
    m_progressPopup = nullptr;
}

TagEditorPanel::TagEditorPanel(Services::Library::TrackDataProvider* provider)
    : m_provider(provider)
{
    setupUI();
}

void TagEditorPanel::setupUI()
{
    auto mkBtn = [this](std::unique_ptr<juce::TextButton>& b, const juce::String& text, juce::Colour c) {
        b = std::make_unique<juce::TextButton>(text);
        b->setColour(juce::TextButton::buttonColourId, c);
        b->setColour(juce::TextButton::textColourOffId,
                     c == Colors::bgLighter() ? Colors::textPrimary() : juce::Colours::white);
        addAndMakeVisible(*b);
    };

    mkBtn(m_backBtn, juce::String::fromUTF8("\xe2\x86\x90 Biblioth\xc3\xa8que"), Colors::bgLighter());
    m_backBtn->onClick = [this] { if (onClose) onClose(); };

    mkBtn(m_selectAllBtn, juce::String::fromUTF8("Tout cocher"), Colors::bgLighter());
    m_selectAllBtn->setTooltip(juce::String::fromUTF8(
        "Coche / d\xc3\xa9""coche tous les titres. Les op\xc3\xa9rations par lot (\xc3\xa9""criture, casse, "
        "renommage, pochettes, recherche en ligne) n'agissent que sur les titres coch\xc3\xa9s."));
    m_selectAllBtn->onClick = [this] {
        int nb = 0;
        for (const auto& it : m_items) if (it.included) ++nb;
        const bool on = nb < (int) m_items.size();
        for (auto& it : m_items) it.included = on;
        m_lastToggledRow = -1;
        if (m_fileList) m_fileList->repaint();
        updateBatchUi();
    };

    mkBtn(m_writeBtn, juce::String::fromUTF8("\xc3\x89""crire les tags"), Colors::primary());
    m_writeBtn->onClick = [this] { writeTags(); };

    mkBtn(m_renameBtn, juce::String::fromUTF8("Renommer les fichiers"), Colors::bgLighter());
    m_renameBtn->onClick = [this] { renameFromMask(); };

    mkBtn(m_fromNameBtn, juce::String::fromUTF8("Tags depuis le nom"), Colors::bgLighter());
    m_fromNameBtn->onClick = [this] { tagsFromFilename(); };

    mkBtn(m_caseBtn, juce::String::fromUTF8("Casse \xe2\x96\xbe"), Colors::bgLighter());
    m_caseBtn->onClick = [this] { showCaseMenu(); };

    mkBtn(m_cleanBtn, juce::String::fromUTF8("Nettoyer \xe2\x96\xbe"), Colors::bgLighter());
    m_cleanBtn->onClick = [this] { showCleanMenu(); };

    mkBtn(m_coverBtn, juce::String::fromUTF8("Pochette \xe2\x96\xbe"), Colors::bgLighter());
    m_coverBtn->onClick = [this] { showCoverMenu(); };

    mkBtn(m_onlineBtn, juce::String::fromUTF8("En ligne \xe2\x96\xbe"), Colors::secondary().darker(0.3f));
    m_onlineBtn->setTooltip(juce::String::fromUTF8(
        "Compl\xc3\xa9tion automatique depuis Internet : tags (titre, artiste, album, "
        "genre, ann\xc3\xa9""e) et pochettes \xe2\x80\x94 ou tout \xc3\xa0 la fois."));
    m_onlineBtn->onClick = [this] {
        juce::PopupMenu m;
        m.addItem(1, juce::String::fromUTF8("Tout automatique (tags + BPM + pochettes)"));
        m.addSeparator();
        m.addItem(2, juce::String::fromUTF8("Compl\xc3\xa9ter les tags + BPM"));
        m.addItem(3, juce::String::fromUTF8("Chercher les pochettes"));
        m.addSeparator();
        m.addItem(4, juce::String::fromUTF8("Analyser avec BeatMate (BPM pr\xc3\xa9""cis, cl\xc3\xa9, \xc3\xa9nergie, mood IA)"),
                  onAnalyzeRequested != nullptr);
        juce::Component::SafePointer<TagEditorPanel> safe(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(m_onlineBtn.get()),
            [safe](int r)
            {
                if (safe == nullptr) return;
                if (r == 1)
                {
                    juce::Component::SafePointer<TagEditorPanel> chain(safe.getComponent());
                    safe->m_afterTagsChain = [chain] { if (chain != nullptr) chain->searchCoversOnline(); };
                    safe->searchTagsOnline();
                }
                else if (r == 2) safe->searchTagsOnline();
                else if (r == 3) safe->searchCoversOnline();
                else if (r == 4 && safe->onAnalyzeRequested)
                {
                    std::vector<int64_t> ids;
                    for (int row : safe->includedRows())
                        if (safe->m_items[(size_t) row].track.id > 0)
                            ids.push_back(safe->m_items[(size_t) row].track.id);
                    if (! ids.empty()) safe->onAnalyzeRequested(std::move(ids));
                }
            });
    };

    m_maskEdit = std::make_unique<juce::TextEditor>();
    m_maskEdit->setText("%artist% - %title%");
    m_maskEdit->setColour(juce::TextEditor::backgroundColourId, Colors::bgLighter());
    m_maskEdit->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
    m_maskEdit->setTooltip(juce::String::fromUTF8(
        "Masque : %artist% %title% %album% %genre% %year% %bpm% %key%"));
    addAndMakeVisible(*m_maskEdit);

    for (int i = 0; i < FCount; ++i)
    {
        auto& f = m_fields[i];
        f.label = std::make_unique<juce::Label>();
        f.label->setText(juce::String::fromUTF8(kFieldNames[i]), juce::dontSendNotification);
        f.label->setColour(juce::Label::textColourId, Colors::textSecondary());
        f.label->setFont(juce::Font(12.0f));
        addAndMakeVisible(*f.label);

        f.editor = std::make_unique<juce::TextEditor>();
        f.editor->setColour(juce::TextEditor::backgroundColourId, Colors::bgLighter());
        f.editor->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
        f.editor->onTextChange = [this, i] { m_fields[i].edited = true; };
        addAndMakeVisible(*f.editor);
    }

    m_fileList = std::make_unique<juce::ListBox>("tagFiles", this);
    m_fileList->setRowHeight(40);
    m_fileList->setMultipleSelectionEnabled(true);
    m_fileList->setColour(juce::ListBox::backgroundColourId, Colors::bgDark());
    addAndMakeVisible(*m_fileList);
}

void TagEditorPanel::setTracks(const std::vector<int64_t>& trackIds, bool preChecked)
{
    m_items.clear();
    if (m_provider)
    {
        for (int64_t id : trackIds)
        {
            auto t = m_provider->getTrack(id);
            if (t.id == 0) continue;
            Item it;
            it.included = preChecked;
            it.track = std::move(t);
            juce::StringArray tags;
            for (const auto& tg : m_provider->getTrackTags(id))
                tags.add(juce::String::fromUTF8(tg.c_str()));
            it.tagsCsv = tags.joinIntoString(", ");
            m_items.push_back(std::move(it));
        }
    }
    m_lastToggledRow = -1;
    if (m_fileList)
    {
        m_fileList->updateContent();
        if (! m_items.empty()) m_fileList->selectRow(0);
    }
    loadFieldsFromSelection();
    updateBatchUi();
}

std::vector<int> TagEditorPanel::selectedRows() const
{
    std::vector<int> out;
    if (! m_fileList) return out;
    auto set = m_fileList->getSelectedRows();
    for (int i = 0; i < set.size(); ++i) out.push_back(set[i]);
    return out;
}

void TagEditorPanel::loadFieldsFromSelection()
{
    const auto rows = selectedRows();

    auto valueOf = [this](int fieldId, const Item& it) -> juce::String {
        const auto& t = it.track;
        switch (fieldId)
        {
            case FTitle:   return juce::String::fromUTF8(t.title.c_str());
            case FArtist:  return juce::String::fromUTF8(t.artist.c_str());
            case FAlbum:   return juce::String::fromUTF8(t.album.c_str());
            case FGenre:   return juce::String::fromUTF8(t.genre.c_str());
            case FYear:    return t.year > 0 ? juce::String(t.year) : juce::String();
            case FBpm:     return t.bpm > 0 ? juce::String(t.bpm, t.bpm == (int) t.bpm ? 0 : 1) : juce::String();
            case FKey:     return juce::String::fromUTF8(t.camelotKey.empty() ? t.key.c_str() : t.camelotKey.c_str());
            case FEnergy:  return t.energy > 0 ? juce::String((int) (t.energy + 0.5f)) : juce::String();
            case FLabel:   return juce::String::fromUTF8(t.label.c_str());
            case FMood:    return juce::String::fromUTF8(t.mood.c_str());
            case FComment: return juce::String::fromUTF8(t.comment.c_str());
            case FMyTags:  return it.tagsCsv;
            default:       return {};
        }
    };

    for (int fi = 0; fi < FCount; ++fi)
    {
        auto& f = m_fields[fi];
        f.edited = false;
        f.multiple = false;
        if (rows.empty())
        {
            f.editor->setText({}, juce::dontSendNotification);
            continue;
        }
        juce::String common = valueOf(fi, m_items[(size_t) rows[0]]);
        for (size_t k = 1; k < rows.size(); ++k)
        {
            if (valueOf(fi, m_items[(size_t) rows[k]]) != common)
            {
                f.multiple = true;
                break;
            }
        }
        f.editor->setText(f.multiple ? kMultiple : common, juce::dontSendNotification);
        f.editor->applyColourToAllText(f.multiple ? Colors::textMuted() : Colors::textPrimary());
    }

    refreshCoverPreview();
}

void TagEditorPanel::refreshCoverPreview()
{
    m_coverPreview = {};
    m_coverLoaded = false;
    const auto rows = selectedRows();
    if (! rows.empty())
    {
        Services::Library::TrackMetadata meta;
        auto bytes = meta.readAlbumArt(m_items[(size_t) rows[0]].track.filePath);
        if (! bytes.empty())
            m_coverPreview = juce::ImageFileFormat::loadFrom(bytes.data(), bytes.size());
        m_coverLoaded = m_coverPreview.isValid();
    }
    repaint();
}

void TagEditorPanel::selectedRowsChanged(int) { loadFieldsFromSelection(); }

int TagEditorPanel::getNumRows() { return (int) m_items.size(); }

void TagEditorPanel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) m_items.size()) return;
    const auto& it = m_items[(size_t) row];

    if (selected) { g.setColour(Colors::primary().withAlpha(0.18f)); g.fillRect(0, 0, w, h); }
    else if (row % 2 == 1) { g.setColour(juce::Colours::white.withAlpha(0.025f)); g.fillRect(0, 0, w, h); }

    juce::Rectangle<float> box(9.0f, h * 0.5f - 7.0f, 14.0f, 14.0f);
    g.setColour(it.included ? Colors::primary() : Colors::bgLighter());
    g.fillRoundedRectangle(box, 3.0f);
    g.setColour(it.included ? Colors::primary().brighter(0.3f) : Colors::border());
    g.drawRoundedRectangle(box, 3.0f, 1.0f);
    if (it.included)
    {
        juce::Path tick;
        tick.startNewSubPath(box.getX() + 3.2f, box.getCentreY());
        tick.lineTo(box.getX() + 5.8f, box.getBottom() - 3.8f);
        tick.lineTo(box.getRight() - 3.0f, box.getY() + 4.0f);
        g.setColour(juce::Colours::white);
        g.strokePath(tick, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }

    const int tx = 32;
    g.setColour(it.dirty ? Colors::warning() : (it.included ? Colors::textPrimary() : Colors::textMuted()));
    g.setFont(juce::Font(12.5f, juce::Font::bold));
    juce::File f(juce::String::fromUTF8(it.track.filePath.c_str()));
    g.drawText((it.dirty ? juce::String::fromUTF8("\xe2\x97\x8f ") : juce::String())
               + f.getFileName(), tx, 4, w - tx - 10, 17, juce::Justification::centredLeft, true);
    g.setColour(Colors::textMuted());
    g.setFont(juce::Font(11.0f));
    juce::String sub = juce::String::fromUTF8(it.track.artist.c_str());
    if (! it.track.title.empty())
        sub << (sub.isNotEmpty() ? " \xe2\x80\x93 " : "") << juce::String::fromUTF8(it.track.title.c_str());
    g.drawText(sub, tx, 21, w - tx - 10, 15, juce::Justification::centredLeft, true);
}

void TagEditorPanel::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) m_items.size()) return;
    if (e.x >= 30) return;

    auto& it = m_items[(size_t) row];
    if (e.mods.isShiftDown() && m_lastToggledRow >= 0 && m_lastToggledRow < (int) m_items.size())
    {
        const bool on = ! it.included;
        const int a = juce::jmin(m_lastToggledRow, row);
        const int b = juce::jmax(m_lastToggledRow, row);
        for (int r = a; r <= b; ++r) m_items[(size_t) r].included = on;
    }
    else
    {
        it.included = ! it.included;
    }
    m_lastToggledRow = row;
    if (m_fileList) m_fileList->repaint();
    updateBatchUi();
}

std::vector<int> TagEditorPanel::includedRows() const
{
    std::vector<int> out;
    for (int i = 0; i < (int) m_items.size(); ++i)
        if (m_items[(size_t) i].included) out.push_back(i);
    if (out.empty()) out = selectedRows();
    return out;
}

void TagEditorPanel::updateBatchUi()
{
    int nb = 0;
    for (const auto& it : m_items) if (it.included) ++nb;
    if (m_selectAllBtn)
        m_selectAllBtn->setButtonText(nb >= (int) m_items.size() && ! m_items.empty()
            ? juce::String::fromUTF8("Tout d\xc3\xa9""cocher")
            : juce::String::fromUTF8("Tout cocher"));
    if (m_writeBtn)
        m_writeBtn->setButtonText(nb > 1
            ? juce::String::fromUTF8("\xc3\x89""crire les tags (") + juce::String(nb) + ")"
            : juce::String::fromUTF8("\xc3\x89""crire les tags"));
    repaint();
}

void TagEditorPanel::writeTags()
{
    const auto rows = includedRows();
    if (rows.empty() || ! m_provider) return;

    int written = 0, failed = 0;
    Services::Library::TrackMetadata meta;

    for (int r : rows)
    {
        auto& it = m_items[(size_t) r];
        auto& t = it.track;
        auto apply = [this](int fi, std::string& target) {
            const auto& f = m_fields[fi];
            const auto txt = f.editor->getText();
            if (txt != kMultiple) target = txt.trim().toStdString();
        };
        apply(FTitle, t.title);
        apply(FArtist, t.artist);
        apply(FAlbum, t.album);
        apply(FGenre, t.genre);
        if (m_fields[FYear].editor->getText() != kMultiple)
            t.year = m_fields[FYear].editor->getText().getIntValue();
        {
            const auto bpmTxt = m_fields[FBpm].editor->getText().trim();
            if (bpmTxt != kMultiple && bpmTxt.isNotEmpty() && bpmTxt.getDoubleValue() > 0)
                t.bpm = bpmTxt.getDoubleValue();
            const auto keyTxt = m_fields[FKey].editor->getText().trim();
            if (keyTxt != kMultiple && keyTxt.isNotEmpty())
            {
                t.key = keyTxt.toStdString();
                const bool isCamelot = keyTxt.length() <= 3
                    && keyTxt.retainCharacters("0123456789").isNotEmpty()
                    && (keyTxt.endsWithIgnoreCase("A") || keyTxt.endsWithIgnoreCase("B"));
                if (isCamelot) t.camelotKey = keyTxt.toUpperCase().toStdString();
            }
            const auto energyTxt = m_fields[FEnergy].editor->getText().trim();
            if (energyTxt != kMultiple && energyTxt.isNotEmpty() && energyTxt.getIntValue() > 0)
                t.energy = (float) juce::jlimit(1, 10, energyTxt.getIntValue());
        }
        apply(FLabel, t.label);
        apply(FMood, t.mood);
        apply(FComment, t.comment);

        const bool fileOk = meta.writeMetadata(t.filePath, t);
        m_provider->updateTrack(t);

        if (m_fields[FMyTags].editor->getText() != kMultiple)
        {
            std::vector<std::string> tags;
            for (const auto& part : juce::StringArray::fromTokens(m_fields[FMyTags].editor->getText(), ",;", ""))
                if (part.trim().isNotEmpty()) tags.push_back(part.trim().toStdString());
            m_provider->setTrackTags(t.id, tags);
            juce::StringArray shown;
            for (const auto& tg : tags) shown.add(juce::String::fromUTF8(tg.c_str()));
            it.tagsCsv = shown.joinIntoString(", ");
        }

        it.dirty = false;
        fileOk ? ++written : ++failed;
    }

    if (m_fileList) m_fileList->repaint();
    if (onTracksChanged) onTracksChanged();

    juce::String msg;
    msg << written << juce::String::fromUTF8(" fichier(s) \xc3\xa9""crit(s)");
    if (failed > 0) msg << ", " << failed << juce::String::fromUTF8(" \xc3\xa9""chec(s) (fichier prot\xc3\xa9g\xc3\xa9 ?)");
    Widgets::ToastNotifier::getInstance().show(
        juce::String::fromUTF8("Tags \xc3\xa9""crits"), msg,
        failed > 0 ? Widgets::ToastNotifier::Kind::Warning : Widgets::ToastNotifier::Kind::Success, 5000);
}

juce::String TagEditorPanel::applyMask(const juce::String& mask, const Models::Track& t)
{
    juce::String out = mask;
    out = out.replace("%artist%", juce::String::fromUTF8(t.artist.c_str()));
    out = out.replace("%title%",  juce::String::fromUTF8(t.title.c_str()));
    out = out.replace("%album%",  juce::String::fromUTF8(t.album.c_str()));
    out = out.replace("%genre%",  juce::String::fromUTF8(t.genre.c_str()));
    out = out.replace("%year%",   t.year > 0 ? juce::String(t.year) : juce::String());
    out = out.replace("%bpm%",    t.bpm > 0 ? juce::String((int) (t.bpm + 0.5)) : juce::String());
    out = out.replace("%key%",    juce::String::fromUTF8(t.camelotKey.empty() ? t.key.c_str() : t.camelotKey.c_str()));
    for (auto c : { '\\', '/', ':', '*', '?', '"', '<', '>', '|' })
        out = out.replaceCharacter(c, '-');
    while (out.contains("  ")) out = out.replace("  ", " ");
    return out.trim();
}

void TagEditorPanel::renameFromMask()
{
    const auto rows = includedRows();
    if (rows.empty() || ! m_provider) return;
    const juce::String mask = m_maskEdit->getText().trim();
    if (mask.isEmpty()) return;

    int renamed = 0, failed = 0;
    for (int r : rows)
    {
        auto& it = m_items[(size_t) r];
        auto& t = it.track;
        juce::File src(juce::String::fromUTF8(t.filePath.c_str()));
        if (! src.existsAsFile()) { ++failed; continue; }
        const juce::String newName = applyMask(mask, t);
        if (newName.isEmpty()) { ++failed; continue; }
        juce::File dst = src.getSiblingFile(newName + src.getFileExtension());
        if (dst == src) continue;
        if (dst.existsAsFile()) { ++failed; continue; }
        if (! src.moveFileTo(dst)) { ++failed; continue; }
        t.filePath = dst.getFullPathName().toStdString();
        m_provider->updateTrack(t);
        ++renamed;
    }

    if (m_fileList) m_fileList->updateContent();
    if (onTracksChanged) onTracksChanged();

    juce::String msg;
    msg << renamed << juce::String::fromUTF8(" fichier(s) renomm\xc3\xa9(s)");
    if (failed > 0) msg << ", " << failed << juce::String::fromUTF8(" ignor\xc3\xa9(s)");
    Widgets::ToastNotifier::getInstance().show(
        juce::String::fromUTF8("Renommage termin\xc3\xa9"), msg,
        failed > 0 ? Widgets::ToastNotifier::Kind::Warning : Widgets::ToastNotifier::Kind::Success, 5000);
}

void TagEditorPanel::tagsFromFilename()
{
    const auto rows = includedRows();
    if (rows.empty()) return;

    int applied = 0;
    for (int r : rows)
    {
        auto& it = m_items[(size_t) r];
        juce::File f(juce::String::fromUTF8(it.track.filePath.c_str()));
        juce::String base = f.getFileNameWithoutExtension();
        base = cleanedTagText(base);
        const int sep = base.indexOf(" - ");
        if (sep <= 0) continue;
        it.track.artist = base.substring(0, sep).trim().toStdString();
        it.track.title = base.substring(sep + 3).trim().toStdString();
        it.dirty = true;
        ++applied;
    }
    if (m_fileList) m_fileList->repaint();
    loadFieldsFromSelection();
    Widgets::ToastNotifier::getInstance().show(
        juce::String::fromUTF8("Tags depuis le nom"),
        juce::String(applied) + juce::String::fromUTF8(" morceau(x) analys\xc3\xa9(s) \xe2\x80\x94 cliquez \xc2\xab \xc3\x89""crire les tags \xc2\xbb pour enregistrer."),
        Widgets::ToastNotifier::Kind::Info, 6000);
}

void TagEditorPanel::applyCase(int mode)
{
    const auto rows = includedRows();
    for (int r : rows)
    {
        auto& it = m_items[(size_t) r];
        auto transform = [mode](std::string& s) {
            juce::String j = juce::String::fromUTF8(s.c_str());
            switch (mode)
            {
                case 0: j = toTitleCase(j); break;
                case 1: j = j.toUpperCase(); break;
                case 2: j = j.toLowerCase(); break;
                case 3: j = j.isEmpty() ? j
                        : juce::String::charToString(juce::CharacterFunctions::toUpperCase(j[0]))
                          + j.substring(1).toLowerCase();
                    break;
                default: break;
            }
            s = j.toStdString();
        };
        transform(it.track.title);
        transform(it.track.artist);
        transform(it.track.album);
        it.dirty = true;
    }
    if (m_fileList) m_fileList->repaint();
    loadFieldsFromSelection();
}

void TagEditorPanel::cleanupTags()
{
    const auto rows = includedRows();
    for (int r : rows)
    {
        auto& it = m_items[(size_t) r];
        auto clean = [](std::string& s) {
            s = cleanedTagText(juce::String::fromUTF8(s.c_str())).toStdString();
        };
        clean(it.track.title);
        clean(it.track.artist);
        clean(it.track.album);
        it.dirty = true;
    }
    if (m_fileList) m_fileList->repaint();
    loadFieldsFromSelection();
}

void TagEditorPanel::showCaseMenu()
{
    juce::PopupMenu m;
    m.addItem(1, "Title Case (Chaque Mot)");
    m.addItem(2, "MAJUSCULES");
    m.addItem(3, "minuscules");
    m.addItem(4, juce::String::fromUTF8("Premi\xc3\xa8re lettre seulement"));
    juce::Component::SafePointer<TagEditorPanel> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(m_caseBtn.get()),
        [safe](int r) { if (safe != nullptr && r > 0) safe->applyCase(r - 1); });
}

void TagEditorPanel::showCleanMenu()
{
    juce::PopupMenu m;
    m.addItem(1, juce::String::fromUTF8("Nettoyer titre/artiste/album\n(_ \xe2\x86\x92 espaces, doubles espaces, suffixes promo)"));
    juce::Component::SafePointer<TagEditorPanel> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(m_cleanBtn.get()),
        [safe](int r) { if (safe != nullptr && r == 1) safe->cleanupTags(); });
}

void TagEditorPanel::showCoverMenu()
{
    juce::PopupMenu m;
    m.addItem(4, juce::String::fromUTF8("Chercher sur Internet\xe2\x80\xa6"));
    m.addSeparator();
    m.addItem(1, juce::String::fromUTF8("Importer une image\xe2\x80\xa6"));
    m.addItem(2, juce::String::fromUTF8("Exporter la pochette\xe2\x80\xa6"), m_coverLoaded);
    m.addItem(3, juce::String::fromUTF8("Retirer la pochette"), m_coverLoaded);
    juce::Component::SafePointer<TagEditorPanel> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(m_coverBtn.get()),
        [safe](int r)
        {
            if (safe == nullptr) return;
            if (r == 1) safe->importCover();
            else if (r == 2) safe->exportCover();
            else if (r == 3) safe->removeCover();
            else if (r == 4) safe->searchCoversOnline();
        });
}

namespace {

std::vector<uint8_t> httpDownloadBytes(const juce::String& urlStr)
{
    juce::URL u(urlStr);
    auto stream = u.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(9000));
    if (! stream) return {};
    juce::MemoryBlock mb;
    stream->readIntoMemoryBlock(mb);
    if (mb.getSize() < 1024) return {};
    return { (const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + mb.getSize() };
}

juce::StringArray itunesCoverUrls(const juce::String& artist, const juce::String& albumOrTitle, int limit)
{
    juce::String term = (artist + " " + albumOrTitle).trim();
    if (term.isEmpty()) return {};
    juce::URL url("https://itunes.apple.com/search");
    url = url.withParameter("term", term)
             .withParameter("media", "music")
             .withParameter("limit", juce::String(juce::jlimit(1, 12, limit)));
    auto stream = url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(9000));
    if (! stream) return {};
    auto parsed = juce::JSON::parse(stream->readEntireStreamAsString());
    juce::StringArray out;
    if (auto* obj = parsed.getDynamicObject())
    {
        auto results = obj->getProperty("results");
        if (auto* arr = results.getArray())
        {
            for (const auto& item : *arr)
            {
                if (auto* it = item.getDynamicObject())
                {
                    juce::String art = it->getProperty("artworkUrl100").toString();
                    if (art.isEmpty()) continue;
                    art = art.replace("100x100", "600x600");
                    out.addIfNotAlreadyThere(art);
                }
            }
        }
    }
    return out;
}

juce::StringArray deezerCoverUrls(const juce::String& artist, const juce::String& albumOrTitle, int limit)
{
    juce::String term = (artist + " " + albumOrTitle).trim();
    if (term.isEmpty()) return {};
    juce::URL url("https://api.deezer.com/search");
    url = url.withParameter("q", term)
             .withParameter("limit", juce::String(juce::jlimit(1, 12, limit)));
    auto stream = url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(9000));
    if (! stream) return {};
    auto parsed = juce::JSON::parse(stream->readEntireStreamAsString());
    juce::StringArray out;
    if (auto* obj = parsed.getDynamicObject())
    {
        if (auto* arr = obj->getProperty("data").getArray())
        {
            for (const auto& item : *arr)
            {
                if (auto* it = item.getDynamicObject())
                {
                    auto album = it->getProperty("album");
                    if (auto* al = album.getDynamicObject())
                    {
                        juce::String art = al->getProperty("cover_xl").toString();
                        if (art.isEmpty()) art = al->getProperty("cover_big").toString();
                        if (art.isNotEmpty()) out.addIfNotAlreadyThere(art);
                    }
                }
            }
        }
    }
    return out;
}

class CoverPickerComponent : public juce::Component
{
public:
    CoverPickerComponent(std::vector<std::pair<juce::Image, std::vector<uint8_t>>> covers,
                         std::function<void(const std::vector<uint8_t>&)> onPick)
        : m_covers(std::move(covers)), m_onPick(std::move(onPick))
    {
        setSize(4 * 170 + 24, ((int) (m_covers.size() + 3) / 4) * 170 + 24);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(Colors::bgDark());
        for (int i = 0; i < (int) m_covers.size(); ++i)
        {
            auto cell = cellBounds(i).reduced(6).toFloat();
            const bool hov = (i == m_hovered);
            if (hov)
            {
                g.setColour(Colors::primary());
                g.drawRoundedRectangle(cell.expanded(3.0f), 8.0f, 2.0f);
            }
            juce::Path clip;
            clip.addRoundedRectangle(cell, 6.0f);
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(clip);
            g.drawImage(m_covers[(size_t) i].first, cell, juce::RectanglePlacement::fillDestination);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        int h = -1;
        for (int i = 0; i < (int) m_covers.size(); ++i)
            if (cellBounds(i).contains(e.getPosition())) { h = i; break; }
        if (h != m_hovered) { m_hovered = h; repaint(); }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        for (int i = 0; i < (int) m_covers.size(); ++i)
        {
            if (cellBounds(i).contains(e.getPosition()))
            {
                if (m_onPick) m_onPick(m_covers[(size_t) i].second);
                if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                    dw->exitModalState(1);
                return;
            }
        }
    }

private:
    juce::Rectangle<int> cellBounds(int i) const
    {
        return { 12 + (i % 4) * 170, 12 + (i / 4) * 170, 170, 170 };
    }
    std::vector<std::pair<juce::Image, std::vector<uint8_t>>> m_covers;
    std::function<void(const std::vector<uint8_t>&)> m_onPick;
    int m_hovered = -1;
};

}

void TagEditorPanel::searchTagsOnline()
{
    const auto rows = includedRows();
    if (rows.empty()) return;

    struct Job { int row; juce::String artist, title, fileBase; };
    auto jobs = std::make_shared<std::vector<Job>>();
    for (int r : rows)
    {
        const auto& t = m_items[(size_t) r].track;
        Job j;
        j.row = r;
        j.artist = juce::String::fromUTF8(t.artist.c_str());
        j.title = juce::String::fromUTF8(t.title.c_str());
        j.fileBase = cleanedTagText(juce::File(juce::String::fromUTF8(t.filePath.c_str()))
                                        .getFileNameWithoutExtension());
        jobs->push_back(std::move(j));
    }

    auto cancel = std::make_shared<std::atomic<bool>>(false);
    openBatchProgress(juce::String::fromUTF8("Recherche des tags en ligne"), cancel);

    juce::Component::SafePointer<TagEditorPanel> safe(this);
    juce::Thread::launch([safe, jobs, cancel]
    {
        int found = 0, missed = 0, idx = 0;
        for (const auto& job : *jobs)
        {
            if (cancel->load()) break;
            ++idx;
            const bool hasLocalTags = job.artist.isNotEmpty() && job.title.isNotEmpty();
            const juce::String qArtist = searchClean(job.artist);
            const juce::String qTitle  = searchClean(job.title);
            const juce::String qFile   = searchClean(job.fileBase);
            juce::StringArray terms;
            auto addTerm = [&terms](const juce::String& t) {
                const auto v = t.trim();
                if (v.length() >= 3) terms.addIfNotAlreadyThere(v);
            };
            addTerm(qArtist + " " + qTitle);
            addTerm(searchCore(qArtist) + " " + searchCore(qTitle));
            addTerm(qFile);
            addTerm(searchCore(qFile));
            addTerm(qTitle);
            addTerm(searchCore(qTitle));
            if (terms.isEmpty()) addTerm(job.fileBase);
            const juce::String localRef = hasLocalTags ? (qArtist + " " + qTitle) : qFile;
            const double threshold = hasLocalTags ? 0.42 : 0.32;
            const juce::String term = terms.isEmpty() ? juce::String() : terms[0];

            juce::String title, artist, album, genre;
            int year = 0;
            double onlineBpm = 0.0;
            bool ok = false;

            if (term.isNotEmpty())
            {
                juce::URL durl("https://api.deezer.com/search");
                durl = durl.withParameter("q", term).withParameter("limit", "3");
                if (auto ds = durl.createInputStream(
                        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                            .withConnectionTimeoutMs(9000)))
                {
                    auto dparsed = juce::JSON::parse(ds->readEntireStreamAsString());
                    if (auto* dobj = dparsed.getDynamicObject())
                        if (auto* darr = dobj->getProperty("data").getArray())
                            for (const auto& item : *darr)
                            {
                                auto* dt = item.getDynamicObject();
                                if (! dt) continue;
                                juce::String cArtist;
                                if (auto* aobj = dt->getProperty("artist").getDynamicObject())
                                    cArtist = aobj->getProperty("name").toString();
                                const juce::String cTitle = dt->getProperty("title").toString();
                                const juce::String cand = cArtist + " " + cTitle;
                                if (isSuspiciousCandidate(localRef, cand)) continue;
                                if (onlineMatchScore(localRef, cand) < threshold) continue;
                                const auto trackId = dt->getProperty("id").toString();
                                if (trackId.isEmpty()) continue;
                                juce::URL turl("https://api.deezer.com/track/" + trackId);
                                if (auto ts = turl.createInputStream(
                                        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                            .withConnectionTimeoutMs(9000)))
                                {
                                    auto tparsed = juce::JSON::parse(ts->readEntireStreamAsString());
                                    if (auto* tobj = tparsed.getDynamicObject())
                                        onlineBpm = (double) tobj->getProperty("bpm");
                                }
                                break;
                            }
                }
            }

            for (const auto& attempt : terms)
            {
                if (ok || cancel->load()) break;
                juce::URL url("https://itunes.apple.com/search");
                url = url.withParameter("term", attempt)
                         .withParameter("media", "music")
                         .withParameter("entity", "song")
                         .withParameter("limit", "8");
                auto stream = url.createInputStream(
                    juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                        .withConnectionTimeoutMs(9000));
                if (stream)
                {
                    auto parsed = juce::JSON::parse(stream->readEntireStreamAsString());
                    double bestScore = 0.0;
                    if (auto* obj = parsed.getDynamicObject())
                    {
                        if (auto* arr = obj->getProperty("results").getArray())
                        {
                            for (const auto& item : *arr)
                            {
                                auto* it = item.getDynamicObject();
                                if (! it) continue;
                                const juce::String cTitle = it->getProperty("trackName").toString();
                                const juce::String cArtist = it->getProperty("artistName").toString();
                                if (cTitle.isEmpty() || cArtist.isEmpty()) continue;
                                const juce::String cand = cArtist + " " + cTitle;
                                if (isSuspiciousCandidate(localRef, cand)) continue;
                                const double score = onlineMatchScore(localRef, cand);
                                if (score <= bestScore) continue;
                                bestScore = score;
                                title = cTitle;
                                artist = cArtist;
                                album = it->getProperty("collectionName").toString();
                                genre = it->getProperty("primaryGenreName").toString();
                                year = it->getProperty("releaseDate").toString().substring(0, 4).getIntValue();
                            }
                        }
                    }
                    ok = bestScore >= threshold;
                    if (! ok) { title.clear(); artist.clear(); album.clear(); genre.clear(); year = 0; }
                }
            }
            ok ? ++found : ++missed;

            const int row = job.row;
            const int done = idx;
            const juce::String progressLine = job.artist.isNotEmpty()
                ? job.artist + " \xe2\x80\x93 " + job.title : job.fileBase;
            juce::MessageManager::callAsync(
                [safe, row, done, total = (int) jobs->size(), progressLine,
                 ok, title, artist, album, genre, year, onlineBpm]
            {
                if (safe == nullptr) return;
                if (row >= 0 && row < (int) safe->m_items.size())
                {
                    auto& t = safe->m_items[(size_t) row].track;
                    if (ok)
                    {
                        t.title = title.toStdString();
                        t.artist = artist.toStdString();
                        if (album.isNotEmpty()) t.album = album.toStdString();
                        if (genre.isNotEmpty()) t.genre = genre.toStdString();
                        if (year > 1900) t.year = year;
                        safe->m_items[(size_t) row].dirty = true;
                    }
                    if (onlineBpm > 40.0 && t.bpm <= 0.0)
                    {
                        t.bpm = onlineBpm;
                        safe->m_items[(size_t) row].dirty = true;
                    }
                }
                safe->updateBatchProgress(done, total, progressLine);
                if (done >= total)
                {
                    if (safe->m_fileList) safe->m_fileList->repaint();
                    safe->loadFieldsFromSelection();
                }
            });
        }

        juce::MessageManager::callAsync([safe, found, missed]
        {
            if (safe != nullptr)
            {
                safe->closeBatchProgress();
                if (safe->m_fileList) safe->m_fileList->repaint();
                safe->loadFieldsFromSelection();
            }
            juce::String msg;
            msg << found << juce::String::fromUTF8(" morceau(x) compl\xc3\xa9t\xc3\xa9(s)");
            if (missed > 0) msg << ", " << missed << juce::String::fromUTF8(" non reconnu(s) (aucun tag faux appliqu\xc3\xa9)");
            msg << juce::String::fromUTF8(" \xe2\x80\x94 v\xc3\xa9rifiez puis \xc2\xab \xc3\x89""crire les tags \xc2\xbb.");
            Widgets::ToastNotifier::getInstance().show(
                juce::String::fromUTF8("Tags en ligne"), msg,
                missed > 0 ? Widgets::ToastNotifier::Kind::Warning : Widgets::ToastNotifier::Kind::Success, 8000);
            if (safe != nullptr && safe->m_afterTagsChain)
            {
                auto next = std::move(safe->m_afterTagsChain);
                safe->m_afterTagsChain = nullptr;
                next();
            }
        });
    });
}

void TagEditorPanel::invalidateCoverCache(int64_t trackId)
{
    if (trackId <= 0) return;
    juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("BeatMate").getChildFile("artcache")
        .getChildFile(juce::String(trackId) + ".png").deleteFile();
}

void TagEditorPanel::applyCoverToRows(const std::vector<uint8_t>& bytes, const std::vector<int>& rows)
{
    if (bytes.empty()) return;
    Services::Library::TrackMetadata meta;
    int done = 0;
    for (int r : rows)
    {
        if (r < 0 || r >= (int) m_items.size()) continue;
        auto& it = m_items[(size_t) r];
        if (meta.writeAlbumArt(it.track.filePath, bytes))
        {
            invalidateCoverCache(it.track.id);
            ++done;
        }
    }
    refreshCoverPreview();
    if (onTracksChanged) onTracksChanged();
    Widgets::ToastNotifier::getInstance().show(
        juce::String::fromUTF8("Pochette appliqu\xc3\xa9""e"),
        juce::String(done) + juce::String::fromUTF8(" fichier(s) mis \xc3\xa0 jour"),
        Widgets::ToastNotifier::Kind::Success, 4000);
}

void TagEditorPanel::searchCoversOnline()
{
    const auto rows = includedRows();
    if (rows.empty()) return;

    juce::Component::SafePointer<TagEditorPanel> safe(this);

    if (rows.size() == 1)
    {
        const auto& t = m_items[(size_t) rows[0]].track;
        const juce::String artist = juce::String::fromUTF8(t.artist.c_str());
        const juce::String album = juce::String::fromUTF8(t.album.c_str());
        const juce::String title = juce::String::fromUTF8(t.title.c_str());

        const int toastId = Widgets::ToastNotifier::getInstance().show(
            juce::String::fromUTF8("Recherche de pochettes\xe2\x80\xa6"),
            artist + " \xe2\x80\x93 " + (album.isNotEmpty() ? album : title),
            Widgets::ToastNotifier::Kind::Progress, 0);

        juce::Thread::launch([safe, artist, album, title, rows, toastId]
        {
            auto urls = deezerCoverUrls(artist, album.isNotEmpty() ? album : title, 6);
            if (urls.isEmpty() && album.isNotEmpty())
                urls = deezerCoverUrls(artist, title, 6);
            for (const auto& u : itunesCoverUrls(artist, album.isNotEmpty() ? album : title, 4))
                urls.addIfNotAlreadyThere(u);
            if (urls.isEmpty())
                for (const auto& u : itunesCoverUrls(artist, title, 6))
                    urls.addIfNotAlreadyThere(u);

            std::vector<std::pair<juce::Image, std::vector<uint8_t>>> covers;
            for (const auto& u : urls)
            {
                auto bytes = httpDownloadBytes(u);
                if (bytes.empty()) continue;
                auto img = juce::ImageFileFormat::loadFrom(bytes.data(), bytes.size());
                if (img.isValid()) covers.emplace_back(std::move(img), std::move(bytes));
                if ((int) covers.size() >= 8) break;
            }

            juce::MessageManager::callAsync([safe, covers = std::move(covers), rows, toastId]() mutable
            {
                Widgets::ToastNotifier::getInstance().dismiss(toastId);
                if (safe == nullptr) return;
                if (covers.empty())
                {
                    Widgets::ToastNotifier::getInstance().show(
                        juce::String::fromUTF8("Aucune pochette trouv\xc3\xa9""e"),
                        juce::String::fromUTF8("V\xc3\xa9rifiez l'artiste et le titre/album."),
                        Widgets::ToastNotifier::Kind::Warning, 5000);
                    return;
                }
                juce::Component::SafePointer<TagEditorPanel> safe2(safe.getComponent());
                auto* picker = new CoverPickerComponent(std::move(covers),
                    [safe2, rows](const std::vector<uint8_t>& bytes)
                    {
                        if (safe2 != nullptr) safe2->applyCoverToRows(bytes, rows);
                    });
                juce::DialogWindow::LaunchOptions opts;
                opts.content.setOwned(picker);
                opts.dialogTitle = juce::String::fromUTF8("Choisir une pochette");
                opts.dialogBackgroundColour = Colors::bgDark();
                opts.escapeKeyTriggersCloseButton = true;
                opts.useNativeTitleBar = true;
                opts.resizable = false;
                opts.launchAsync();
            });
        });
        return;
    }

    struct Job { int row; juce::String artist, album, title; };
    auto jobs = std::make_shared<std::vector<Job>>();
    for (int r : rows)
    {
        const auto& t = m_items[(size_t) r].track;
        jobs->push_back({ r, juce::String::fromUTF8(t.artist.c_str()),
                          juce::String::fromUTF8(t.album.c_str()),
                          juce::String::fromUTF8(t.title.c_str()) });
    }

    auto cancel = std::make_shared<std::atomic<bool>>(false);
    openBatchProgress(juce::String::fromUTF8("Recherche de pochettes"), cancel);

    juce::Thread::launch([safe, jobs, cancel]
    {
        int found = 0, missed = 0, idx = 0;
        for (const auto& job : *jobs)
        {
            if (cancel->load()) break;
            ++idx;
            auto urls = deezerCoverUrls(job.artist, job.album.isNotEmpty() ? job.album : job.title, 3);
            if (urls.isEmpty())
                urls = deezerCoverUrls(job.artist, job.title, 3);
            if (urls.isEmpty())
                urls = itunesCoverUrls(job.artist, job.title, 3);
            std::vector<uint8_t> bytes;
            if (! urls.isEmpty()) bytes = httpDownloadBytes(urls[0]);

            const int row = job.row;
            const int done = idx;
            const juce::String progressLine = job.artist + " \xe2\x80\x93 " + job.title;
            bytes.empty() ? ++missed : ++found;
            juce::MessageManager::callAsync([safe, bytes = std::move(bytes), row, done, progressLine,
                                             total = (int) jobs->size()]() mutable
            {
                if (safe == nullptr) return;
                if (! bytes.empty())
                    safe->applyCoverToRows(bytes, { row });
                safe->updateBatchProgress(done, total, progressLine);
            });
        }

        juce::MessageManager::callAsync([safe, found, missed]
        {
            if (safe != nullptr) safe->closeBatchProgress();
            juce::String msg;
            msg << found << juce::String::fromUTF8(" pochette(s) trouv\xc3\xa9""e(s)");
            if (missed > 0) msg << ", " << missed << juce::String::fromUTF8(" introuvable(s)");
            Widgets::ToastNotifier::getInstance().show(
                juce::String::fromUTF8("Recherche termin\xc3\xa9""e"), msg,
                missed > 0 ? Widgets::ToastNotifier::Kind::Warning : Widgets::ToastNotifier::Kind::Success, 6000);
        });
    });
}

void TagEditorPanel::importCover()
{
    m_chooser = std::make_unique<juce::FileChooser>(
        juce::String::fromUTF8("Choisir une image de pochette"),
        juce::File::getSpecialLocation(juce::File::userPicturesDirectory),
        "*.jpg;*.jpeg;*.png");
    juce::Component::SafePointer<TagEditorPanel> safe(this);
    m_chooser->launchAsync(juce::FileBrowserComponent::openMode,
        [safe](const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File() || safe == nullptr) return;
            juce::MemoryBlock data;
            if (! f.loadFileAsData(data) || data.getSize() == 0) return;
            std::vector<uint8_t> bytes((const uint8_t*) data.getData(),
                                       (const uint8_t*) data.getData() + data.getSize());
            Services::Library::TrackMetadata meta;
            int done = 0;
            for (int r : safe->includedRows())
                if (meta.writeAlbumArt(safe->m_items[(size_t) r].track.filePath, bytes)) ++done;
            safe->refreshCoverPreview();
            Widgets::ToastNotifier::getInstance().show(
                juce::String::fromUTF8("Pochette import\xc3\xa9""e"),
                juce::String(done) + juce::String::fromUTF8(" fichier(s) mis \xc3\xa0 jour"),
                Widgets::ToastNotifier::Kind::Success, 4000);
        });
}

void TagEditorPanel::exportCover()
{
    const auto rows = selectedRows();
    if (rows.empty()) return;
    Services::Library::TrackMetadata meta;
    auto bytes = meta.readAlbumArt(m_items[(size_t) rows[0]].track.filePath);
    if (bytes.empty()) return;
    const bool isPng = bytes.size() > 4 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G';
    m_chooser = std::make_unique<juce::FileChooser>(
        juce::String::fromUTF8("Exporter la pochette"),
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
            .getChildFile(juce::String("pochette") + (isPng ? ".png" : ".jpg")),
        isPng ? "*.png" : "*.jpg");
    auto shared = std::make_shared<std::vector<uint8_t>>(std::move(bytes));
    m_chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [shared](const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            f.replaceWithData(shared->data(), shared->size());
        });
}

void TagEditorPanel::removeCover()
{
    Services::Library::TrackMetadata meta;
    int done = 0;
    for (int r : includedRows())
        if (meta.writeAlbumArt(m_items[(size_t) r].track.filePath, {})) ++done;
    refreshCoverPreview();
    Widgets::ToastNotifier::getInstance().show(
        juce::String::fromUTF8("Pochette retir\xc3\xa9""e"),
        juce::String(done) + juce::String::fromUTF8(" fichier(s) mis \xc3\xa0 jour"),
        Widgets::ToastNotifier::Kind::Info, 4000);
}

void TagEditorPanel::paint(juce::Graphics& g)
{
    g.fillAll(Colors::bgDark());
    g.setColour(Colors::textPrimary());
    g.setFont(juce::Font(18.0f, juce::Font::bold));
    g.drawText(juce::String::fromUTF8("\xc3\x89""diteur de tags"), 20, 12, 400, 26,
               juce::Justification::centredLeft);
    g.setColour(Colors::textSecondary());
    g.setFont(juce::Font(12.0f));
    int included = 0;
    for (const auto& it : m_items) if (it.included) ++included;
    g.drawText(juce::String(m_items.size()) + juce::String::fromUTF8(" morceau(x) \xc2\xb7 ")
               + juce::String(included) + juce::String::fromUTF8(" coch\xc3\xa9(s) pour le lot"),
               20, 38, 400, 16, juce::Justification::centredLeft);

    g.setColour(Colors::textMuted());
    g.setFont(juce::Font(11.5f));
    g.drawText(juce::String::fromUTF8(
                   "Cliquez un titre pour \xc3\xa9""diter ses champs \xc2\xb7 cochez plusieurs titres "
                   "(Maj+clic pour une plage) pour agir en masse \xe2\x80\x94 seuls les titres "
                   "coch\xc3\xa9s sont modifi\xc3\xa9s."),
               430, 38, getWidth() - 460, 16, juce::Justification::centredLeft, true);

    if (m_items.empty())
    {
        auto area = getLocalBounds().reduced(60).withTrimmedTop(120).withTrimmedBottom(80);
        g.setColour(Colors::textMuted());
        g.setFont(juce::Font(15.0f, juce::Font::bold));
        g.drawText(juce::String::fromUTF8("Aucun morceau \xc3\xa0 taguer"),
                   area.removeFromTop(28), juce::Justification::centredTop);
        g.setFont(juce::Font(13.0f));
        g.drawFittedText(juce::String::fromUTF8(
                             "Retournez dans la Biblioth\xc3\xa8que, cochez les titres \xc3\xa0 modifier "
                             "(ou \xc2\xab Tout cocher \xc2\xbb), puis rouvrez \xc2\xab \xc3\x89""diteur de tags \xc2\xbb.\n\n"
                             "Sans case coch\xc3\xa9""e, tous les morceaux affich\xc3\xa9s sont charg\xc3\xa9s."),
                         area.removeFromTop(70), juce::Justification::centredTop, 4);
    }

    auto cover = getLocalBounds().removeFromRight(340).reduced(16).removeFromTop(130)
                     .withTrimmedTop(48).withSizeKeepingCentre(88, 88).toFloat();
    if (m_coverLoaded && m_coverPreview.isValid())
    {
        juce::Path clip;
        clip.addRoundedRectangle(cover, 6.0f);
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(clip);
        g.drawImage(m_coverPreview, cover, juce::RectanglePlacement::fillDestination);
    }
    else
    {
        g.setColour(Colors::bgLighter());
        g.fillRoundedRectangle(cover, 6.0f);
        g.setColour(Colors::textMuted());
        g.setFont(juce::Font(10.5f));
        g.drawFittedText(juce::String::fromUTF8("Sans pochette"), cover.toNearestInt(),
                         juce::Justification::centred, 2);
    }
}

void TagEditorPanel::resized()
{
    auto area = getLocalBounds().reduced(16);
    auto top = area.removeFromTop(34);
    m_backBtn->setBounds(top.removeFromRight(130).reduced(0, 2));

    area.removeFromTop(26);

    auto actions = area.removeFromBottom(38);
    m_writeBtn->setBounds(actions.removeFromLeft(140).reduced(0, 3)); actions.removeFromLeft(8);
    m_maskEdit->setBounds(actions.removeFromLeft(190).reduced(0, 5)); actions.removeFromLeft(6);
    m_renameBtn->setBounds(actions.removeFromLeft(160).reduced(0, 3)); actions.removeFromLeft(6);
    m_fromNameBtn->setBounds(actions.removeFromLeft(150).reduced(0, 3)); actions.removeFromLeft(6);
    m_caseBtn->setBounds(actions.removeFromLeft(85).reduced(0, 3)); actions.removeFromLeft(6);
    m_cleanBtn->setBounds(actions.removeFromLeft(100).reduced(0, 3)); actions.removeFromLeft(6);
    m_coverBtn->setBounds(actions.removeFromLeft(105).reduced(0, 3)); actions.removeFromLeft(6);
    m_onlineBtn->setBounds(actions.removeFromLeft(115).reduced(0, 3));
    area.removeFromBottom(8);

    auto right = area.removeFromRight(340);
    right.removeFromTop(130);
    const int fieldH = juce::jmax(34, juce::jmin(44, right.getHeight() / FCount));
    for (int i = 0; i < FCount; ++i)
    {
        auto row = right.removeFromTop(fieldH);
        m_fields[i].label->setBounds(row.removeFromTop(16));
        m_fields[i].editor->setBounds(row.reduced(0, 2));
    }

    area.removeFromRight(12);
    auto listTop = area.removeFromTop(26);
    m_selectAllBtn->setBounds(listTop.removeFromLeft(140));
    if (m_fileList) m_fileList->setBounds(area.withTrimmedTop(4));
}

}
