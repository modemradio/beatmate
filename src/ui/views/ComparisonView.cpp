#include "ComparisonView.h"
#include "../styles/ColorPalette.h"
#include "../widgets/ToastNotifier.h"
#include "../utils/ViewPrefs.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <map>

namespace BeatMate::UI {

namespace {

const char* kStateNames[] = { "A seulement", "B seulement", "Diff\xc3\xa9rents", "Identiques" };

juce::Colour stateColour(int s)
{
    switch (s)
    {
        case 0: return juce::Colour(0xfff97316);   // A seulement — orange
        case 1: return juce::Colour(0xff3b82f6);   // B seulement — bleu
        case 2: return juce::Colour(0xffef4444);   // Différents — rouge
        default: return juce::Colour(0xff10b981);  // Identiques — vert
    }
}

bool isAudioFile(const juce::File& f)
{
    static const char* exts[] = { ".mp3", ".wav", ".flac", ".aac", ".m4a",
                                  ".ogg", ".aiff", ".aif", ".wma" };
    const auto e = f.getFileExtension().toLowerCase();
    for (auto* x : exts) if (e == x) return true;
    return false;
}

} // namespace

ComparisonView::ComparisonView() { setupUI(); }
ComparisonView::ComparisonView(Services::Library::TrackDataProvider* provider)
    : m_provider(provider)
{
    setupUI();
}

ComparisonView::~ComparisonView()
{
    m_cancel.store(true);
    if (m_worker.joinable()) m_worker.join();
}

void ComparisonView::setupUI()
{
    auto mkBtn = [this](std::unique_ptr<juce::TextButton>& b, const juce::String& text, juce::Colour c) {
        b = std::make_unique<juce::TextButton>(text);
        b->setColour(juce::TextButton::buttonColourId, c);
        b->setColour(juce::TextButton::textColourOffId,
                     c == Colors::bgLighter() ? Colors::textPrimary() : juce::Colours::white);
        addAndMakeVisible(*b);
    };

    auto mkDirLabel = [this](std::unique_ptr<juce::Label>& l) {
        l = std::make_unique<juce::Label>();
        l->setColour(juce::Label::textColourId, Colors::textPrimary());
        l->setColour(juce::Label::backgroundColourId, Colors::bgLighter());
        l->setFont(juce::Font(12.5f));
        l->setMinimumHorizontalScale(0.7f);
        addAndMakeVisible(*l);
    };
    mkDirLabel(m_dirALabel);
    mkDirLabel(m_dirBLabel);

    m_dirA = juce::File(juce::String(Prefs::getString("compare.dirA", "")));
    m_dirB = juce::File(juce::String(Prefs::getString("compare.dirB", "")));
    if (m_dirA.getFullPathName().isNotEmpty()) m_dirALabel->setText(m_dirA.getFullPathName(), juce::dontSendNotification);
    if (m_dirB.getFullPathName().isNotEmpty()) m_dirBLabel->setText(m_dirB.getFullPathName(), juce::dontSendNotification);

    mkBtn(m_pickABtn, juce::String::fromUTF8("Dossier A\xe2\x80\xa6"), Colors::bgLighter());
    m_pickABtn->onClick = [this] { pickFolder(true); };
    mkBtn(m_pickBBtn, juce::String::fromUTF8("Dossier B\xe2\x80\xa6"), Colors::bgLighter());
    m_pickBBtn->onClick = [this] { pickFolder(false); };
    mkBtn(m_openABtn, "A", Colors::bgLighter());
    m_openABtn->onClick = [this] { if (m_dirA.isDirectory()) m_dirA.revealToUser(); };
    m_openABtn->setTooltip(juce::String::fromUTF8("Ouvrir le dossier A dans l'explorateur"));
    mkBtn(m_openBBtn, "B", Colors::bgLighter());
    m_openBBtn->onClick = [this] { if (m_dirB.isDirectory()) m_dirB.revealToUser(); };
    m_openBBtn->setTooltip(juce::String::fromUTF8("Ouvrir le dossier B dans l'explorateur"));

    mkBtn(m_compareBtn, juce::String::fromUTF8("Comparer"), Colors::primary());
    m_compareBtn->onClick = [this] {
        if (m_scanning.load()) cancelCompare();
        else startCompare();
    };

    m_recurseCheck = std::make_unique<juce::ToggleButton>(juce::String::fromUTF8("Sous-dossiers"));
    m_recurseCheck->setToggleState(true, juce::dontSendNotification);
    m_recurseCheck->setColour(juce::ToggleButton::textColourId, Colors::textSecondary());
    addAndMakeVisible(*m_recurseCheck);

    m_audioOnlyCheck = std::make_unique<juce::ToggleButton>(juce::String::fromUTF8("Audio seulement"));
    m_audioOnlyCheck->setToggleState(true, juce::dontSendNotification);
    m_audioOnlyCheck->setColour(juce::ToggleButton::textColourId, Colors::textSecondary());
    addAndMakeVisible(*m_audioOnlyCheck);

    m_criteriaCombo = std::make_unique<juce::ComboBox>();
    m_criteriaCombo->addItem(juce::String::fromUTF8("Nom seul (pr\xc3\xa9sence)"), 1);
    m_criteriaCombo->addItem(juce::String::fromUTF8("Nom + taille (recommand\xc3\xa9)"), 2);
    m_criteriaCombo->addItem(juce::String::fromUTF8("Nom + taille + date (strict)"), 3);
    m_criteriaCombo->setTooltip(juce::String::fromUTF8(
        "Nom seul : deux fichiers de m\xc3\xaame nom sont dits identiques, m\xc3\xaame si leur "
        "contenu diff\xc3\xa8re.\nNom + taille : d\xc3\xa9tecte les versions diff\xc3\xa9rentes.\n"
        "Nom + taille + date : signale aussi une simple recopie (tol\xc3\xa9rance de 2 s)."));
    m_criteriaCombo->setSelectedId(2, juce::dontSendNotification);
    addAndMakeVisible(*m_criteriaCombo);

    for (int i = 0; i < 4; ++i)
    {
        auto& chip = m_stateChips[i];
        chip = std::make_unique<juce::TextButton>(juce::String::fromUTF8(kStateNames[i]));
        chip->setClickingTogglesState(true);
        chip->setToggleState(m_show[i], juce::dontSendNotification);
        chip->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
        chip->setColour(juce::TextButton::buttonOnColourId, stateColour(i).darker(0.15f));
        chip->setColour(juce::TextButton::textColourOffId, Colors::textSecondary());
        chip->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        chip->onClick = [this, i] {
            m_show[i] = m_stateChips[i]->getToggleState();
            rebuildFiltered();
        };
        addAndMakeVisible(*chip);
    }

    mkBtn(m_checkAllBtn, juce::String::fromUTF8("Tout cocher"), Colors::bgLighter());
    m_checkAllBtn->setTooltip(juce::String::fromUTF8(
        "Coche toutes les lignes affich\xc3\xa9""es. Seules les lignes coch\xc3\xa9""es sont copi\xc3\xa9""es."));
    m_checkAllBtn->onClick = [this] { setAllChecked(true); };
    mkBtn(m_uncheckAllBtn, juce::String::fromUTF8("Tout d\xc3\xa9""cocher"), Colors::bgLighter());
    m_uncheckAllBtn->onClick = [this] { setAllChecked(false); };

    mkBtn(m_copyABBtn, juce::String::fromUTF8("Copier A \xe2\x86\x92 B"), Colors::bgLighter());
    m_copyABBtn->onClick = [this] { copyFiles(true); };
    mkBtn(m_copyBABtn, juce::String::fromUTF8("Copier B \xe2\x86\x92 A"), Colors::bgLighter());
    m_copyBABtn->onClick = [this] { copyFiles(false); };
    mkBtn(m_exportBtn, juce::String::fromUTF8("Rapport CSV"), Colors::bgLighter());
    m_exportBtn->onClick = [this] { exportCsv(); };

    m_list = std::make_unique<juce::ListBox>("compareList", this);
    m_list->setRowHeight(30);
    m_list->setMultipleSelectionEnabled(true);
    m_list->setColour(juce::ListBox::backgroundColourId, Colors::bgDark());
    addAndMakeVisible(*m_list);
}

void ComparisonView::pickFolder(bool isA)
{
    m_chooser = std::make_unique<juce::FileChooser>(
        isA ? juce::String::fromUTF8("Choisir le dossier A")
            : juce::String::fromUTF8("Choisir le dossier B"),
        (isA ? m_dirA : m_dirB).isDirectory() ? (isA ? m_dirA : m_dirB)
            : juce::File::getSpecialLocation(juce::File::userMusicDirectory));
    juce::Component::SafePointer<ComparisonView> safe(this);
    m_chooser->launchAsync(juce::FileBrowserComponent::openMode
                           | juce::FileBrowserComponent::canSelectDirectories,
        [safe, isA](const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File() || safe == nullptr) return;
            if (isA) { safe->m_dirA = f; safe->m_dirALabel->setText(f.getFullPathName(), juce::dontSendNotification); }
            else     { safe->m_dirB = f; safe->m_dirBLabel->setText(f.getFullPathName(), juce::dontSendNotification); }
            Prefs::setString(isA ? "compare.dirA" : "compare.dirB", f.getFullPathName().toStdString());
        });
}

void ComparisonView::cancelCompare()
{
    m_cancel.store(true);
}

void ComparisonView::startCompare()
{
    if (! m_dirA.isDirectory() || ! m_dirB.isDirectory())
    {
        Widgets::ToastNotifier::getInstance().show(
            juce::String::fromUTF8("Comparaison"),
            juce::String::fromUTF8("Choisissez d'abord les deux dossiers."),
            Widgets::ToastNotifier::Kind::Warning, 5000);
        return;
    }
    if (m_worker.joinable()) m_worker.join();

    m_scanning.store(true);
    m_cancel.store(false);
    m_compareBtn->setButtonText(juce::String::fromUTF8("Stop"));

    const bool recurse = m_recurseCheck->getToggleState();
    const bool audioOnly = m_audioOnlyCheck->getToggleState();
    const int criteria = m_criteriaCombo->getSelectedId();
    const juce::File dirA = m_dirA, dirB = m_dirB;

    const int toastId = Widgets::ToastNotifier::getInstance().show(
        juce::String::fromUTF8("Comparaison en cours\xe2\x80\xa6"),
        dirA.getFileName() + juce::String::fromUTF8("  \xe2\x87\x84  ") + dirB.getFileName(),
        Widgets::ToastNotifier::Kind::Progress, 0);

    juce::Component::SafePointer<ComparisonView> safe(this);
    m_worker = std::thread([safe, dirA, dirB, recurse, audioOnly, criteria, toastId]
    {
        auto listDir = [&](const juce::File& root) {
            std::map<juce::String, juce::File> out;   // clé relative minuscule -> fichier
            const int flags = juce::File::findFiles | juce::File::ignoreHiddenFiles;
            auto files = root.findChildFiles(flags, recurse, "*");
            for (const auto& f : files)
            {
                if (safe == nullptr || safe->m_cancel.load()) return out;
                if (audioOnly && ! isAudioFile(f)) continue;
                auto rel = f.getRelativePathFrom(root);
                out[rel.toLowerCase()] = f;
            }
            return out;
        };

        auto mapA = listDir(dirA);
        auto mapB = listDir(dirB);

        std::vector<Row> rows;
        rows.reserve(mapA.size() + mapB.size());

        for (const auto& kv : mapA)
        {
            if (safe == nullptr || safe->m_cancel.load()) break;
            Row r;
            r.rel = kv.second.getRelativePathFrom(dirA);
            r.sizeA = (int64_t) kv.second.getSize();
            r.timeA = kv.second.getLastModificationTime().toMilliseconds();
            auto itB = mapB.find(kv.first);
            if (itB == mapB.end())
            {
                r.state = State::OnlyA;
            }
            else
            {
                r.sizeB = (int64_t) itB->second.getSize();
                r.timeB = itB->second.getLastModificationTime().toMilliseconds();
                bool same = true;
                if (criteria >= 2 && r.sizeA != r.sizeB) same = false;
                if (criteria >= 3 && std::abs(r.timeA - r.timeB) > 2000) same = false;  // tolérance FAT 2 s
                r.state = same ? State::Identical : State::Different;
                mapB.erase(itB);
            }
            rows.push_back(std::move(r));
        }
        for (const auto& kv : mapB)
        {
            if (safe == nullptr || safe->m_cancel.load()) break;
            Row r;
            r.rel = kv.second.getRelativePathFrom(dirB);
            r.state = State::OnlyB;
            r.sizeB = (int64_t) kv.second.getSize();
            r.timeB = kv.second.getLastModificationTime().toMilliseconds();
            rows.push_back(std::move(r));
        }

        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            if (a.state != b.state) return (int) a.state < (int) b.state;
            return a.rel.compareIgnoreCase(b.rel) < 0;
        });

        juce::MessageManager::callAsync([safe, rows = std::move(rows), toastId]() mutable
        {
            Widgets::ToastNotifier::getInstance().dismiss(toastId);
            if (safe == nullptr) return;
            safe->m_scanning.store(false);
            safe->m_compareBtn->setButtonText(juce::String::fromUTF8("Comparer"));
            safe->m_rows = std::move(rows);
            safe->rebuildFiltered();
            int counts[4] = { 0, 0, 0, 0 };
            for (const auto& r : safe->m_rows) counts[(int) r.state]++;
            juce::String msg;
            msg << counts[0] << juce::String::fromUTF8(" A-seul \xc2\xb7 ")
                << counts[1] << juce::String::fromUTF8(" B-seul \xc2\xb7 ")
                << counts[2] << juce::String::fromUTF8(" diff\xc3\xa9rents \xc2\xb7 ")
                << counts[3] << juce::String::fromUTF8(" identiques");
            Widgets::ToastNotifier::getInstance().show(
                juce::String::fromUTF8("Comparaison termin\xc3\xa9""e"), msg,
                Widgets::ToastNotifier::Kind::Success, 7000);
        });
    });
}

void ComparisonView::rebuildFiltered()
{
    m_filtered.clear();
    for (int i = 0; i < (int) m_rows.size(); ++i)
        if (m_show[(int) m_rows[(size_t) i].state])
            m_filtered.push_back(i);
    updateChips();
    updateCopyButtons();
    if (m_list) m_list->updateContent();
    repaint();
}

void ComparisonView::updateChips()
{
    int counts[4] = { 0, 0, 0, 0 };
    for (const auto& r : m_rows) counts[(int) r.state]++;
    for (int i = 0; i < 4; ++i)
        if (m_stateChips[i])
            m_stateChips[i]->setButtonText(juce::String::fromUTF8(kStateNames[i])
                                           + "  " + juce::String(counts[i]));
}

void ComparisonView::copyFiles(bool aToB)
{
    if (! m_dirA.isDirectory() || ! m_dirB.isDirectory() || m_scanning.load()) return;

    std::vector<int> toCopy;
    int added = 0, replaced = 0;
    for (int idx : checkedRows())
    {
        const auto& r = m_rows[(size_t) idx];
        const bool missing = aToB ? (r.state == State::OnlyA) : (r.state == State::OnlyB);
        if (missing) { toCopy.push_back(idx); ++added; }
        else if (r.state == State::Different) { toCopy.push_back(idx); ++replaced; }
    }
    if (toCopy.empty())
    {
        Widgets::ToastNotifier::getInstance().show(
            juce::String::fromUTF8("Copie"),
            juce::String::fromUTF8("Aucun fichier coch\xc3\xa9 \xc3\xa0 copier dans cette direction. "
                                   "Cochez des lignes \xc2\xab A seulement \xc2\xbb, \xc2\xab B seulement \xc2\xbb "
                                   "ou \xc2\xab Diff\xc3\xa9rents \xc2\xbb."),
            Widgets::ToastNotifier::Kind::Info, 6000);
        return;
    }

    juce::Component::SafePointer<ComparisonView> safe(this);
    const juce::String dirLabel = aToB ? juce::String::fromUTF8("A \xe2\x86\x92 B")
                                       : juce::String::fromUTF8("B \xe2\x86\x92 A");
    juce::String detail;
    detail << added << juce::String::fromUTF8(" fichier(s) manquant(s) seront ajout\xc3\xa9s.\n");
    if (replaced > 0)
        detail << replaced << juce::String::fromUTF8(" fichier(s) existant(s) seront \xc3\x89""CRAS\xc3\x89S "
                                                     "et ne pourront pas \xc3\xaatre r\xc3\xa9""cup\xc3\xa9r\xc3\xa9s.\n");
    detail << juce::String::fromUTF8("\nPour n'ajouter que les manquants, d\xc3\xa9""cochez les lignes "
                                     "\xc2\xab Diff\xc3\xa9rents \xc2\xbb avant de copier.\n\nContinuer ?");
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon,
        juce::String::fromUTF8("Copier ") + dirLabel,
        detail,
        juce::String::fromUTF8("Copier"), juce::String::fromUTF8("Annuler"), nullptr,
        juce::ModalCallbackFunction::create([safe, toCopy, aToB](int res)
        {
            if (res != 1 || safe == nullptr) return;
            if (safe->m_worker.joinable()) safe->m_worker.join();

            const juce::File srcRoot = aToB ? safe->m_dirA : safe->m_dirB;
            const juce::File dstRoot = aToB ? safe->m_dirB : safe->m_dirA;
            std::vector<juce::String> rels;
            for (int idx : toCopy) rels.push_back(safe->m_rows[(size_t) idx].rel);

            const int toastId = Widgets::ToastNotifier::getInstance().show(
                juce::String::fromUTF8("Copie en cours\xe2\x80\xa6"),
                juce::String("0/") + juce::String((int) rels.size()),
                Widgets::ToastNotifier::Kind::Progress, 0);

            safe->m_scanning.store(true);
            safe->m_cancel.store(false);
            safe->m_worker = std::thread([safe, srcRoot, dstRoot, rels, toastId]
            {
                int done = 0, failed = 0, idx2 = 0;
                for (const auto& rel : rels)
                {
                    if (safe == nullptr || safe->m_cancel.load()) break;
                    ++idx2;
                    juce::File src = srcRoot.getChildFile(rel);
                    juce::File dst = dstRoot.getChildFile(rel);
                    dst.getParentDirectory().createDirectory();
                    bool ok = src.existsAsFile();
                    if (ok && dst.existsAsFile()) ok = dst.deleteFile();
                    if (ok) ok = src.copyFileTo(dst);
                    ok ? ++done : ++failed;
                    const int shown = idx2;
                    juce::MessageManager::callAsync([toastId, shown, total = (int) rels.size()]
                    {
                        Widgets::ToastNotifier::getInstance().update(toastId,
                            juce::String(shown) + "/" + juce::String(total));
                    });
                }
                juce::MessageManager::callAsync([safe, toastId, done, failed]
                {
                    Widgets::ToastNotifier::getInstance().dismiss(toastId);
                    if (safe == nullptr) return;
                    safe->m_scanning.store(false);
                    juce::String msg;
                    msg << done << juce::String::fromUTF8(" fichier(s) copi\xc3\xa9(s)");
                    if (failed > 0) msg << ", " << failed << juce::String::fromUTF8(" \xc3\xa9""chec(s)");
                    Widgets::ToastNotifier::getInstance().show(
                        juce::String::fromUTF8("Copie termin\xc3\xa9""e"), msg,
                        failed > 0 ? Widgets::ToastNotifier::Kind::Warning
                                   : Widgets::ToastNotifier::Kind::Success, 6000);
                    safe->startCompare();   // re-comparer pour refléter l'état réel
                });
            });
        }));
}

void ComparisonView::exportCsv()
{
    if (m_rows.empty()) return;
    m_chooser = std::make_unique<juce::FileChooser>(
        juce::String::fromUTF8("Exporter le rapport de comparaison"),
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
            .getChildFile("comparaison.csv"), "*.csv");
    auto rows = m_rows;
    const juce::String pathA = m_dirA.getFullPathName(), pathB = m_dirB.getFullPathName();
    m_chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [rows, pathA, pathB](const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            juce::String csv;
            csv << juce::String::fromUTF8("\xc3\x89tat;Fichier;Taille A;Taille B;Date A;Date B\r\n");
            csv << "A;" << pathA << ";;;;\r\n" << "B;" << pathB << ";;;;\r\n";
            for (const auto& r : rows)
            {
                auto field = [](const juce::String& s) {
                    return s.containsAnyOf(";\"\n") ? "\"" + s.replace("\"", "\"\"") + "\"" : s;
                };
                csv << juce::String::fromUTF8(kStateNames[(int) r.state]) << ";"
                    << field(r.rel) << ";"
                    << (r.sizeA >= 0 ? juce::String(r.sizeA) : juce::String()) << ";"
                    << (r.sizeB >= 0 ? juce::String(r.sizeB) : juce::String()) << ";"
                    << fmtDate(r.timeA) << ";" << fmtDate(r.timeB) << "\r\n";
            }
            f.replaceWithText("\xEF\xBB\xBF" + csv);
            f.revealToUser();
        });
}

juce::String ComparisonView::fmtSize(int64_t bytes)
{
    if (bytes < 0) return {};
    if (bytes >= 1024LL * 1024 * 1024)
        return juce::String((double) bytes / (1024.0 * 1024.0 * 1024.0), 2) + " Go";
    if (bytes >= 1024 * 1024)
        return juce::String((double) bytes / (1024.0 * 1024.0), 1) + " Mo";
    if (bytes >= 1024)
        return juce::String((double) bytes / 1024.0, 0) + " Ko";
    return juce::String(bytes) + " o";
}

juce::String ComparisonView::fmtDate(int64_t unixMs)
{
    if (unixMs <= 0) return {};
    return juce::Time(unixMs).formatted("%Y-%m-%d %H:%M");
}

int ComparisonView::getNumRows() { return (int) m_filtered.size(); }

void ComparisonView::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) m_filtered.size()) return;
    const auto& r = m_rows[(size_t) m_filtered[(size_t) row]];

    if (selected) { g.setColour(Colors::primary().withAlpha(0.16f)); g.fillRect(0, 0, w, h); }
    else if (row % 2 == 1) { g.setColour(juce::Colours::white.withAlpha(0.022f)); g.fillRect(0, 0, w, h); }

    juce::Rectangle<float> box(8.0f, h * 0.5f - 7.0f, 14.0f, 14.0f);
    g.setColour(r.checked ? Colors::primary() : Colors::bgLighter());
    g.fillRoundedRectangle(box, 3.0f);
    g.setColour(r.checked ? Colors::primary().brighter(0.3f) : Colors::border());
    g.drawRoundedRectangle(box, 3.0f, 1.0f);
    if (r.checked)
    {
        juce::Path tick;
        tick.startNewSubPath(box.getX() + 3.2f, box.getCentreY());
        tick.lineTo(box.getX() + 5.8f, box.getBottom() - 3.8f);
        tick.lineTo(box.getRight() - 3.0f, box.getY() + 4.0f);
        g.setColour(juce::Colours::white);
        g.strokePath(tick, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }

    const auto col = stateColour((int) r.state);
    g.setColour(col.withAlpha(0.18f));
    g.fillRoundedRectangle(30.0f, 5.0f, 96.0f, (float) h - 10.0f, 9.0f);
    g.setColour(col);
    g.setFont(juce::Font(10.5f, juce::Font::bold));
    g.drawText(juce::String::fromUTF8(kStateNames[(int) r.state]), 30, 5, 96, h - 10,
               juce::Justification::centred);

    g.setColour(r.checked ? Colors::textPrimary() : Colors::textMuted());
    g.setFont(juce::Font(12.5f));
    const int wRight = 420;
    g.drawText(r.rel, 136, 0, w - 136 - wRight, h, juce::Justification::centredLeft, true);

    g.setFont(juce::Font(11.5f));
    g.setColour(Colors::textSecondary());
    int x = w - wRight;
    g.drawText(fmtSize(r.sizeA), x, 0, 80, h, juce::Justification::centredRight); x += 86;
    g.drawText(fmtSize(r.sizeB), x, 0, 80, h, juce::Justification::centredRight); x += 92;
    g.setColour(Colors::textMuted());
    g.drawText(fmtDate(r.timeA), x, 0, 116, h, juce::Justification::centredLeft); x += 120;
    g.drawText(fmtDate(r.timeB), x, 0, 116, h, juce::Justification::centredLeft);
}

void ComparisonView::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) m_filtered.size()) return;
    if (e.x >= 28) return;

    auto& r = m_rows[(size_t) m_filtered[(size_t) row]];
    if (e.mods.isShiftDown() && m_lastToggledRow >= 0
        && m_lastToggledRow < (int) m_filtered.size())
    {
        const bool on = ! r.checked;
        const int a = juce::jmin(m_lastToggledRow, row);
        const int b = juce::jmax(m_lastToggledRow, row);
        for (int i = a; i <= b; ++i)
            m_rows[(size_t) m_filtered[(size_t) i]].checked = on;
    }
    else
    {
        r.checked = ! r.checked;
    }
    m_lastToggledRow = row;
    if (m_list) m_list->repaint();
    updateCopyButtons();
}

std::vector<int> ComparisonView::checkedRows() const
{
    std::vector<int> out;
    for (int idx : m_filtered)
        if (m_rows[(size_t) idx].checked) out.push_back(idx);
    return out;
}

void ComparisonView::setAllChecked(bool on)
{
    for (int idx : m_filtered) m_rows[(size_t) idx].checked = on;
    m_lastToggledRow = -1;
    if (m_list) m_list->repaint();
    updateCopyButtons();
}

void ComparisonView::updateCopyButtons()
{
    int ab = 0, ba = 0;
    for (int idx : checkedRows())
    {
        const auto s = m_rows[(size_t) idx].state;
        if (s == State::OnlyA || s == State::Different) ++ab;
        if (s == State::OnlyB || s == State::Different) ++ba;
    }
    if (m_copyABBtn)
    {
        m_copyABBtn->setButtonText(ab > 0
            ? juce::String::fromUTF8("Copier A \xe2\x86\x92 B (") + juce::String(ab) + ")"
            : juce::String::fromUTF8("Copier A \xe2\x86\x92 B"));
        m_copyABBtn->setEnabled(ab > 0);
    }
    if (m_copyBABtn)
    {
        m_copyBABtn->setButtonText(ba > 0
            ? juce::String::fromUTF8("Copier B \xe2\x86\x92 A (") + juce::String(ba) + ")"
            : juce::String::fromUTF8("Copier B \xe2\x86\x92 A"));
        m_copyBABtn->setEnabled(ba > 0);
    }
}

void ComparisonView::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= (int) m_filtered.size()) return;
    const auto& r = m_rows[(size_t) m_filtered[(size_t) row]];
    juce::File f = (r.state == State::OnlyB ? m_dirB : m_dirA).getChildFile(r.rel);
    if (f.existsAsFile()) f.revealToUser();
}

void ComparisonView::paint(juce::Graphics& g)
{
    g.fillAll(Colors::bgDark());
    g.setColour(Colors::textPrimary());
    g.setFont(juce::Font(22.0f, juce::Font::bold));
    g.drawText(juce::String::fromUTF8("Comparaison"), 24, 18, 400, 30, juce::Justification::centredLeft);
    g.setColour(Colors::textSecondary());
    g.setFont(juce::Font(13.0f));
    g.drawText(juce::String::fromUTF8("Deux dossiers c\xc3\xb4te \xc3\xa0 c\xc3\xb4te \xe2\x80\x94 manquants, diff\xc3\xa9rents, copie dans les deux sens, rapport"),
               24, 48, 800, 20, juce::Justification::centredLeft);

    if (m_rows.empty())
    {
        g.setColour(Colors::textMuted());
        g.setFont(juce::Font(14.0f));
        g.drawText(juce::String::fromUTF8("Choisissez les dossiers A et B puis cliquez \xc2\xab Comparer \xc2\xbb."),
                   getLocalBounds().withTrimmedTop(200), juce::Justification::centredTop);
    }
}

void ComparisonView::resized()
{
    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(60);

    auto dirs = area.removeFromTop(32);
    const int half = dirs.getWidth() / 2;
    auto left = dirs.removeFromLeft(half).withTrimmedRight(8);
    m_pickABtn->setBounds(left.removeFromLeft(110));
    left.removeFromLeft(6);
    m_openABtn->setBounds(left.removeFromRight(34));
    left.removeFromRight(6);
    m_dirALabel->setBounds(left);
    auto right = dirs;
    m_pickBBtn->setBounds(right.removeFromLeft(110));
    right.removeFromLeft(6);
    m_openBBtn->setBounds(right.removeFromRight(34));
    right.removeFromRight(6);
    m_dirBLabel->setBounds(right);

    area.removeFromTop(8);
    auto opts = area.removeFromTop(30);
    m_compareBtn->setBounds(opts.removeFromLeft(130).reduced(0, 1));
    opts.removeFromLeft(14);
    m_recurseCheck->setBounds(opts.removeFromLeft(130));
    m_audioOnlyCheck->setBounds(opts.removeFromLeft(150));
    m_criteriaCombo->setBounds(opts.removeFromLeft(170).reduced(0, 2));
    opts.removeFromLeft(14);
    m_exportBtn->setBounds(opts.removeFromRight(110).reduced(0, 1));
    opts.removeFromRight(8);
    m_copyBABtn->setBounds(opts.removeFromRight(150).reduced(0, 1));
    opts.removeFromRight(6);
    m_copyABBtn->setBounds(opts.removeFromRight(150).reduced(0, 1));

    area.removeFromTop(8);
    auto chips = area.removeFromTop(28);
    for (int i = 0; i < 4; ++i)
    {
        if (! m_stateChips[i]) continue;
        m_stateChips[i]->setBounds(chips.removeFromLeft(150).reduced(0, 1));
        chips.removeFromLeft(6);
    }
    if (m_uncheckAllBtn) { m_uncheckAllBtn->setBounds(chips.removeFromRight(120).reduced(0, 1)); chips.removeFromRight(6); }
    if (m_checkAllBtn) m_checkAllBtn->setBounds(chips.removeFromRight(110).reduced(0, 1));

    area.removeFromTop(8);
    if (m_list) m_list->setBounds(area);
}

void ComparisonView::retranslateUi() { repaint(); }

} // namespace BeatMate::UI
