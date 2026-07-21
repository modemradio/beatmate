#include "DuplicateManagerDialog.h"
#include "../styles/ColorPalette.h"
#include "../widgets/ToastNotifier.h"
#include "../../services/library/TrackDataProvider.h"

namespace BeatMate::UI {

namespace {

juce::String fmtSize(int64_t bytes)
{
    if (bytes <= 0) return "-";
    if (bytes < 1024LL * 1024) return juce::String(bytes / 1024.0, 0) + " Ko";
    if (bytes < 1024LL * 1024 * 1024) return juce::String(bytes / (1024.0 * 1024.0), 1) + " Mo";
    return juce::String(bytes / (1024.0 * 1024.0 * 1024.0), 2) + " Go";
}

juce::String fmtDuration(double sec)
{
    if (sec <= 0.0) return "-";
    const int m = (int) (sec / 60.0);
    const int s = (int) sec % 60;
    return juce::String(m) + ":" + juce::String(s).paddedLeft('0', 2);
}

juce::Colour criterionColour(Services::Library::DuplicateScanner::Criterion c)
{
    using C = Services::Library::DuplicateScanner::Criterion;
    switch (c)
    {
        case C::FileIdentical:  return juce::Colour(0xffef4444);
        case C::AudioIdentical: return juce::Colour(0xfff97316);
        case C::Metadata:       return juce::Colour(0xff3b82f6);
        case C::Filename:       return juce::Colour(0xff8b5cf6);
    }
    return juce::Colours::grey;
}

} // namespace

void DuplicateManagerDialog::show(Services::Library::TrackDataProvider* provider,
                                  std::function<void()> onFinished)
{
    if (provider == nullptr) return;
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(new DuplicateManagerDialog(provider, std::move(onFinished)));
    opts.dialogTitle = juce::String::fromUTF8("Doublons \xe2\x80\x94 BeatMate");
    opts.dialogBackgroundColour = Colors::bgDark();
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.launchAsync();
}

DuplicateManagerDialog::DuplicateManagerDialog(Services::Library::TrackDataProvider* provider,
                                               std::function<void()> onFinished)
    : m_provider(provider), m_onFinished(std::move(onFinished))
{
    auto mkBtn = [this](std::unique_ptr<juce::TextButton>& b, const juce::String& t,
                        juce::Colour c, std::function<void()> fn) {
        b = std::make_unique<juce::TextButton>(t);
        b->setColour(juce::TextButton::buttonColourId, c);
        b->setColour(juce::TextButton::textColourOffId,
                     c == Colors::bgLighter() ? Colors::textPrimary() : juce::Colours::white);
        b->onClick = std::move(fn);
        addAndMakeVisible(*b);
    };

    mkBtn(m_scanBtn, juce::String::fromUTF8("Rechercher"), Colors::primary(),
          [this] { if (m_running.load()) cancelScan(); else startScan(); });
    mkBtn(m_removeBtn, juce::String::fromUTF8("Retirer de la biblioth\xc3\xa8que"),
          Colors::warning().darker(0.2f), [this] { removeSelected(false); });
    mkBtn(m_deleteBtn, juce::String::fromUTF8("Supprimer les fichiers"),
          Colors::error().darker(0.2f), [this] { removeSelected(true); });
    mkBtn(m_csvBtn, juce::String::fromUTF8("Rapport CSV"), Colors::bgLighter(),
          [this] { exportCsv(); });
    mkBtn(m_checkAllBtn, juce::String::fromUTF8("Tout cocher"), Colors::bgLighter(),
          [this] {
              for (auto& g : m_groups)
                  for (auto& e : g.entries) e.checked = ! e.keep;
              if (m_list) m_list->repaint();
              updateButtons();
          });
    mkBtn(m_uncheckAllBtn, juce::String::fromUTF8("Tout d\xc3\xa9""cocher"), Colors::bgLighter(),
          [this] {
              for (auto& g : m_groups)
                  for (auto& e : g.entries) e.checked = false;
              if (m_list) m_list->repaint();
              updateButtons();
          });
    mkBtn(m_revealBtn, juce::String::fromUTF8("Ouvrir le dossier"), Colors::bgLighter(),
          [this] {
              const int r = m_list ? m_list->getSelectedRow() : -1;
              if (r < 0 || r >= (int) m_rows.size()) return;
              const auto& row = m_rows[(size_t) r];
              if (row.entry < 0) return;
              juce::File(juce::String::fromUTF8(
                  m_groups[(size_t) row.group].entries[(size_t) row.entry].track.filePath.c_str()))
                  .revealToUser();
          });
    mkBtn(m_closeBtn, juce::String::fromUTF8("Fermer"), Colors::bgLighter(),
          [this] {
              cancelScan();
              if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(0);
          });

    m_keepCombo = std::make_unique<juce::ComboBox>();
    m_keepCombo->addItem(juce::String::fromUTF8("Garder la meilleure qualit\xc3\xa9"), 1);
    m_keepCombo->addItem(juce::String::fromUTF8("Garder la fiche la plus compl\xc3\xa8te"), 2);
    m_keepCombo->addItem(juce::String::fromUTF8("Garder le plus ancien"), 3);
    m_keepCombo->addItem(juce::String::fromUTF8("Garder le plus r\xc3\xa9""cent"), 4);
    m_keepCombo->addItem(juce::String::fromUTF8("Garder le chemin le plus court"), 5);
    m_keepCombo->setSelectedId(1, juce::dontSendNotification);
    m_keepCombo->setTooltip(juce::String::fromUTF8(
        "Regle appliqu\xc3\xa9""e dans chaque groupe pour d\xc3\xa9signer le fichier \xc3\xa0 conserver. "
        "Vous pouvez toujours changer manuellement en cliquant sur \xc2\xab garder \xc2\xbb."));
    m_keepCombo->onChange = [this] { applyKeepRuleToAll(); };
    addAndMakeVisible(*m_keepCombo);

    auto mkToggle = [this](std::unique_ptr<juce::ToggleButton>& t, const juce::String& text,
                           bool on, const juce::String& tip) {
        t = std::make_unique<juce::ToggleButton>(text);
        t->setToggleState(on, juce::dontSendNotification);
        t->setColour(juce::ToggleButton::textColourId, Colors::textSecondary());
        t->setColour(juce::ToggleButton::tickColourId, Colors::primary());
        t->setTooltip(tip);
        addAndMakeVisible(*t);
    };
    mkToggle(m_cFile, juce::String::fromUTF8("Fichier identique"), true,
             juce::String::fromUTF8("M\xc3\xaame taille et m\xc3\xaame empreinte : copie certaine."));
    mkToggle(m_cAudio, juce::String::fromUTF8("Audio identique"), true,
             juce::String::fromUTF8("M\xc3\xaame dur\xc3\xa9""e, m\xc3\xaame BPM, m\xc3\xaame cl\xc3\xa9 : "
                                    "m\xc3\xaame enregistrement dans un autre format."));
    mkToggle(m_cMeta, juce::String::fromUTF8("Artiste + titre"), true,
             juce::String::fromUTF8("Compare les tags apr\xc3\xa8s nettoyage (Original Mix, feat., accents)."));
    mkToggle(m_cName, juce::String::fromUTF8("Nom de fichier"), false,
             juce::String::fromUTF8("Dernier filet : bruyant sur les collections mal nomm\xc3\xa9""es."));
    mkToggle(m_cIgnoreTags, juce::String::fromUTF8("Ignorer (Original Mix), feat., Remastered"), true,
             juce::String::fromUTF8("D\xc3\xa9""cochez pour distinguer un remix de l'original."));

    m_list = std::make_unique<juce::ListBox>("duplicates", this);
    m_list->setRowHeight(30);
    m_list->setColour(juce::ListBox::backgroundColourId, Colors::bgDark());
    addAndMakeVisible(*m_list);

    updateButtons();
    setSize(1080, 660);

    juce::Component::SafePointer<DuplicateManagerDialog> safe(this);
    juce::MessageManager::callAsync([safe] { if (safe != nullptr) safe->startScan(); });
}

DuplicateManagerDialog::~DuplicateManagerDialog()
{
    cancelScan();
    if (m_worker.joinable()) m_worker.join();
}

void DuplicateManagerDialog::cancelScan()
{
    m_cancel.store(true);
    stopTimer();
}

void DuplicateManagerDialog::startScan()
{
    if (m_running.load()) return;
    if (m_worker.joinable()) m_worker.join();

    m_opts.useFileIdentical  = m_cFile && m_cFile->getToggleState();
    m_opts.useAudioIdentical = m_cAudio && m_cAudio->getToggleState();
    m_opts.useMetadata       = m_cMeta && m_cMeta->getToggleState();
    m_opts.useFilename       = m_cName && m_cName->getToggleState();
    m_opts.ignoreRemixTags   = m_cIgnoreTags && m_cIgnoreTags->getToggleState();
    m_opts.keepRule = static_cast<Scanner::KeepRule>(
        juce::jmax(0, (m_keepCombo ? m_keepCombo->getSelectedId() : 1) - 1));

    if (! m_opts.useFileIdentical && ! m_opts.useAudioIdentical
        && ! m_opts.useMetadata && ! m_opts.useFilename)
    {
        Widgets::ToastNotifier::getInstance().show(
            juce::String::fromUTF8("Doublons"),
            juce::String::fromUTF8("Cochez au moins un crit\xc3\xa8re de recherche."),
            Widgets::ToastNotifier::Kind::Info, 4000);
        return;
    }

    m_groups.clear();
    m_rows.clear();
    m_collapsed.clear();
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pending.clear();
    }
    if (m_list) m_list->updateContent();

    m_running.store(true);
    m_cancel.store(false);
    m_done.store(0);
    m_total.store(0);
    m_phase = juce::String::fromUTF8("Analyse");
    updateButtons();
    startTimerHz(8);

    juce::Component::SafePointer<DuplicateManagerDialog> safe(this);
    auto* provider = m_provider;
    auto opts = m_opts;
    m_worker = std::thread([this, safe, provider, opts]
    {
        Services::Library::DuplicateScanner scanner(provider);
        auto groups = scanner.scan(opts, m_cancel,
            [this, safe](int done, int total, const std::string& phase) {
                m_done.store(done);
                m_total.store(total);
                juce::MessageManager::callAsync([safe, phase] {
                    if (safe != nullptr) safe->m_phase = juce::String::fromUTF8(phase.c_str());
                });
            },
            [this](const Services::Library::DuplicateScanner::Group& g) {
                std::lock_guard<std::mutex> lock(m_pendingMutex);
                m_pending.push_back(g);
            });

        juce::MessageManager::callAsync([safe, groups = std::move(groups)]() mutable
        {
            if (safe == nullptr) return;
            safe->m_running.store(false);
            safe->stopTimer();
            {
                std::lock_guard<std::mutex> lock(safe->m_pendingMutex);
                safe->m_pending.clear();
            }
            safe->m_groups = std::move(groups);
            safe->m_collapsed.assign(safe->m_groups.size(), false);
            safe->rebuildRows();
            safe->updateButtons();

            int files = 0;
            int64_t waste = 0;
            for (const auto& g : safe->m_groups)
            {
                files += (int) g.entries.size() - 1;
                waste += g.wastedBytes;
            }
            juce::String msg;
            msg << (int) safe->m_groups.size() << juce::String::fromUTF8(" groupe(s), ")
                << files << juce::String::fromUTF8(" fichier(s) en trop, ")
                << fmtSize(waste) << juce::String::fromUTF8(" r\xc3\xa9""cup\xc3\xa9rables");
            Widgets::ToastNotifier::getInstance().show(
                juce::String::fromUTF8("Recherche termin\xc3\xa9""e"), msg,
                safe->m_groups.empty() ? Widgets::ToastNotifier::Kind::Success
                                       : Widgets::ToastNotifier::Kind::Warning, 8000);
        });
    });
}

void DuplicateManagerDialog::rebuildRows()
{
    m_rows.clear();
    for (int g = 0; g < (int) m_groups.size(); ++g)
    {
        m_rows.push_back({ g, -1 });
        if (g < (int) m_collapsed.size() && m_collapsed[(size_t) g]) continue;
        for (int e = 0; e < (int) m_groups[(size_t) g].entries.size(); ++e)
            m_rows.push_back({ g, e });
    }
    if (m_list) m_list->updateContent();
    repaint();
}

void DuplicateManagerDialog::applyKeepRuleToAll()
{
    const auto rule = static_cast<Scanner::KeepRule>(
        juce::jmax(0, (m_keepCombo ? m_keepCombo->getSelectedId() : 1) - 1));
    Services::Library::DuplicateScanner scanner(m_provider);
    for (auto& g : m_groups) scanner.applyKeepRule(g, rule);
    if (m_list) m_list->repaint();
    updateButtons();
}

int DuplicateManagerDialog::checkedCount() const
{
    int n = 0;
    for (const auto& g : m_groups)
        for (const auto& e : g.entries)
            if (e.checked && ! e.keep) ++n;
    return n;
}

int64_t DuplicateManagerDialog::reclaimableBytes() const
{
    int64_t b = 0;
    for (const auto& g : m_groups)
        for (const auto& e : g.entries)
            if (e.checked && ! e.keep) b += e.track.fileSize;
    return b;
}

void DuplicateManagerDialog::updateButtons()
{
    const int n = checkedCount();
    const bool busy = m_running.load();
    if (m_scanBtn)
        m_scanBtn->setButtonText(busy ? juce::String::fromUTF8("Arr\xc3\xaater")
                                      : juce::String::fromUTF8("Rechercher"));
    if (m_removeBtn)
    {
        m_removeBtn->setButtonText(n > 0
            ? juce::String::fromUTF8("Retirer de la biblioth\xc3\xa8que (") + juce::String(n) + ")"
            : juce::String::fromUTF8("Retirer de la biblioth\xc3\xa8que"));
        m_removeBtn->setEnabled(n > 0 && ! busy);
    }
    if (m_deleteBtn)
    {
        m_deleteBtn->setButtonText(n > 0
            ? juce::String::fromUTF8("Supprimer les fichiers (") + juce::String(n) + ")"
            : juce::String::fromUTF8("Supprimer les fichiers"));
        m_deleteBtn->setEnabled(n > 0 && ! busy);
    }
    if (m_csvBtn) m_csvBtn->setEnabled(! m_groups.empty());
    repaint();
}

void DuplicateManagerDialog::timerCallback()
{
    std::vector<Scanner::Group> fresh;
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        fresh.swap(m_pending);
    }
    if (! fresh.empty())
    {
        for (auto& g : fresh)
        {
            m_groups.push_back(std::move(g));
            m_collapsed.push_back(false);
        }
        rebuildRows();
        updateButtons();
    }
    repaint();
}

int DuplicateManagerDialog::getNumRows() { return (int) m_rows.size(); }

void DuplicateManagerDialog::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) m_rows.size()) return;
    const auto& r = m_rows[(size_t) row];
    const auto& grp = m_groups[(size_t) r.group];

    if (selected) { g.setColour(Colors::primary().withAlpha(0.16f)); g.fillRect(0, 0, w, h); }

    if (r.entry < 0)
    {
        g.setColour(Colors::bgCard());
        g.fillRect(0, 0, w, h);
        const auto col = criterionColour(grp.criterion);
        g.setColour(col);
        g.fillRect(0, 0, 3, h);

        g.setColour(Colors::textPrimary());
        g.setFont(juce::Font(12.5f, juce::Font::bold));
        const bool folded = r.group < (int) m_collapsed.size() && m_collapsed[(size_t) r.group];
        g.drawText((folded ? juce::String::fromUTF8("\xe2\x96\xb8  ")
                           : juce::String::fromUTF8("\xe2\x96\xbe  "))
                       + juce::String::fromUTF8(grp.label.c_str()),
                   12, 0, w - 420, h, juce::Justification::centredLeft, true);

        g.setColour(col);
        g.setFont(juce::Font(10.5f, juce::Font::bold));
        g.drawText(juce::String::fromUTF8(Scanner::criterionLabel(grp.criterion).c_str()),
                   w - 400, 0, 130, h, juce::Justification::centredRight);
        g.setColour(Colors::textMuted());
        g.setFont(juce::Font(11.0f));
        g.drawText(juce::String((int) grp.entries.size()) + juce::String::fromUTF8(" fichiers  \xc2\xb7  ")
                       + fmtSize(grp.wastedBytes) + juce::String::fromUTF8(" r\xc3\xa9""cup\xc3\xa9rables"),
                   w - 262, 0, 250, h, juce::Justification::centredRight);
        return;
    }

    const auto& e = grp.entries[(size_t) r.entry];
    if (! selected && (row % 2) == 1)
    { g.setColour(juce::Colours::white.withAlpha(0.02f)); g.fillRect(0, 0, w, h); }

    if (e.keep)
    {
        g.setColour(Colors::success().withAlpha(0.16f));
        g.fillRoundedRectangle(26.0f, 4.0f, 62.0f, (float) h - 8.0f, 8.0f);
        g.setColour(Colors::success());
        g.setFont(juce::Font(10.5f, juce::Font::bold));
        g.drawText(juce::String::fromUTF8("GARD\xc3\x89"), 26, 4, 62, h - 8, juce::Justification::centred);
    }
    else
    {
        juce::Rectangle<float> box(30.0f, h * 0.5f - 7.0f, 14.0f, 14.0f);
        g.setColour(e.checked ? Colors::error() : Colors::bgLighter());
        g.fillRoundedRectangle(box, 3.0f);
        g.setColour(e.checked ? Colors::error().brighter(0.3f) : Colors::border());
        g.drawRoundedRectangle(box, 3.0f, 1.0f);
        if (e.checked)
        {
            juce::Path tick;
            tick.startNewSubPath(box.getX() + 3.2f, box.getCentreY());
            tick.lineTo(box.getX() + 5.8f, box.getBottom() - 3.8f);
            tick.lineTo(box.getRight() - 3.0f, box.getY() + 4.0f);
            g.setColour(juce::Colours::white);
            g.strokePath(tick, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
        }
        g.setColour(Colors::textMuted());
        g.setFont(juce::Font(10.0f));
        g.drawText(juce::String::fromUTF8("garder"), 50, 0, 40, h, juce::Justification::centredLeft);
    }

    juce::File f(juce::String::fromUTF8(e.track.filePath.c_str()));
    g.setColour(e.keep ? Colors::textPrimary() : Colors::textSecondary());
    g.setFont(juce::Font(11.5f));
    g.drawText(f.getFileName(), 96, 0, w - 96 - 470, h, juce::Justification::centredLeft, true);

    g.setColour(Colors::textMuted());
    g.setFont(juce::Font(10.5f));
    int x = w - 466;
    g.drawText(f.getParentDirectory().getFullPathName(), x, 0, 190, h,
               juce::Justification::centredLeft, true);
    x += 196;
    g.drawText(e.track.bitRate > 0 ? juce::String(e.track.bitRate) + " kbps" : juce::String("-"),
               x, 0, 66, h, juce::Justification::centredRight); x += 70;
    g.drawText(fmtDuration(e.track.duration), x, 0, 46, h, juce::Justification::centredRight); x += 50;
    g.drawText(fmtSize(e.track.fileSize), x, 0, 62, h, juce::Justification::centredRight); x += 66;
    if (! e.keep && ! e.reason.empty())
    {
        g.setColour(Colors::warning().withAlpha(0.85f));
        g.drawText(juce::String::fromUTF8(e.reason.c_str()), x, 0, 120, h,
                   juce::Justification::centredLeft, true);
    }
}

void DuplicateManagerDialog::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) m_rows.size()) return;
    const auto r = m_rows[(size_t) row];

    if (r.entry < 0)
    {
        if (r.group < (int) m_collapsed.size())
            m_collapsed[(size_t) r.group] = ! m_collapsed[(size_t) r.group];
        rebuildRows();
        return;
    }

    auto& grp = m_groups[(size_t) r.group];
    auto& entry = grp.entries[(size_t) r.entry];

    // Clic sur « garder » : ce fichier devient le conserve, les autres passent
    if (! entry.keep && e.x >= 46 && e.x < 92)
    {
        int64_t waste = 0;
        for (int i = 0; i < (int) grp.entries.size(); ++i)
        {
            auto& en = grp.entries[(size_t) i];
            en.keep = (i == r.entry);
            en.checked = ! en.keep;
            if (! en.keep) waste += en.track.fileSize;
        }
        grp.wastedBytes = waste;
        if (m_list) m_list->repaint();
        updateButtons();
        return;
    }

    if (e.x < 46 && ! entry.keep)
    {
        entry.checked = ! entry.checked;
        if (m_list) m_list->repaintRow(row);
        updateButtons();
    }
}

void DuplicateManagerDialog::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= (int) m_rows.size()) return;
    const auto& r = m_rows[(size_t) row];
    if (r.entry < 0) return;
    juce::File(juce::String::fromUTF8(
        m_groups[(size_t) r.group].entries[(size_t) r.entry].track.filePath.c_str()))
        .revealToUser();
}

void DuplicateManagerDialog::removeSelected(bool deleteFiles)
{
    const int n = checkedCount();
    if (n == 0 || ! m_provider) return;

    juce::String msg;
    msg << n << juce::String::fromUTF8(" fichier(s) s\xc3\xa9lectionn\xc3\xa9s, ")
        << fmtSize(reclaimableBytes()) << ".\n\n";
    if (deleteFiles)
        msg << juce::String::fromUTF8(
            "Les fichiers seront envoy\xc3\xa9s \xc3\xa0 la corbeille de Windows et retir\xc3\xa9s "
            "de la biblioth\xc3\xa8que. Vous pourrez les restaurer depuis la corbeille.\n\nContinuer ?");
    else
        msg << juce::String::fromUTF8(
            "Les entr\xc3\xa9""es seront retir\xc3\xa9""es de la biblioth\xc3\xa8que BeatMate. "
            "Les fichiers resteront sur le disque.\n\nContinuer ?");

    juce::Component::SafePointer<DuplicateManagerDialog> safe(this);
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::WarningIcon,
        deleteFiles ? juce::String::fromUTF8("Supprimer les fichiers")
                    : juce::String::fromUTF8("Retirer de la biblioth\xc3\xa8que"),
        msg, juce::String::fromUTF8("Confirmer"), juce::String::fromUTF8("Annuler"), nullptr,
        juce::ModalCallbackFunction::create([safe, deleteFiles](int res)
        {
            if (res != 1 || safe == nullptr) return;
            int removed = 0, failed = 0;
            for (auto& g : safe->m_groups)
            {
                for (auto& e : g.entries)
                {
                    if (! e.checked || e.keep) continue;
                    bool ok = true;
                    if (deleteFiles)
                    {
                        juce::File f(juce::String::fromUTF8(e.track.filePath.c_str()));
                        if (f.existsAsFile()) ok = f.moveToTrash();
                    }
                    if (ok)
                    {
                        safe->m_provider->deleteTrack(e.track.id);
                        e.checked = false;
                        e.reason = "supprime";
                        ++removed;
                    }
                    else ++failed;
                }
            }

            juce::String done;
            done << removed << juce::String::fromUTF8(deleteFiles ? " fichier(s) supprim\xc3\xa9(s)"
                                                                  : " entr\xc3\xa9""e(s) retir\xc3\xa9""e(s)");
            if (failed > 0) done << ", " << failed << juce::String::fromUTF8(" \xc3\xa9""chec(s)");
            Widgets::ToastNotifier::getInstance().show(
                juce::String::fromUTF8("Doublons"), done,
                failed > 0 ? Widgets::ToastNotifier::Kind::Warning
                           : Widgets::ToastNotifier::Kind::Success, 7000);

            if (safe->m_onFinished) safe->m_onFinished();
            safe->startScan();
        }));
}

void DuplicateManagerDialog::exportCsv()
{
    m_chooser = std::make_unique<juce::FileChooser>(
        juce::String::fromUTF8("Exporter le rapport de doublons"),
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
            .getChildFile("doublons-beatmate.csv"), "*.csv");
    auto groups = m_groups;
    m_chooser->launchAsync(juce::FileBrowserComponent::saveMode
                               | juce::FileBrowserComponent::warnAboutOverwriting,
        [groups](const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            auto field = [](juce::String s) {
                s = s.replace("\r", " ").replace("\n", " ");
                return s.containsAnyOf(";\"") ? "\"" + s.replace("\"", "\"\"") + "\"" : s;
            };
            juce::String csv;
            csv << juce::String::fromUTF8(
                "Groupe;Critere;Action;Artiste;Titre;Fichier;Dossier;Debit;Duree;Taille;Motif\r\n");
            int gi = 0;
            for (const auto& g : groups)
            {
                ++gi;
                for (const auto& e : g.entries)
                {
                    juce::File p(juce::String::fromUTF8(e.track.filePath.c_str()));
                    csv << gi << ";"
                        << field(juce::String::fromUTF8(Scanner::criterionLabel(g.criterion).c_str())) << ";"
                        << (e.keep ? "GARDE" : (e.checked ? "A SUPPRIMER" : "ignore")) << ";"
                        << field(juce::String::fromUTF8(e.track.artist.c_str())) << ";"
                        << field(juce::String::fromUTF8(e.track.title.c_str())) << ";"
                        << field(p.getFileName()) << ";"
                        << field(p.getParentDirectory().getFullPathName()) << ";"
                        << e.track.bitRate << ";"
                        << juce::String(e.track.duration, 1) << ";"
                        << e.track.fileSize << ";"
                        << field(juce::String::fromUTF8(e.reason.c_str())) << "\r\n";
                }
            }
            f.replaceWithText("\xEF\xBB\xBF" + csv);
            f.revealToUser();
        });
}

void DuplicateManagerDialog::paint(juce::Graphics& g)
{
    g.fillAll(Colors::bgDark());

    g.setColour(Colors::textPrimary());
    g.setFont(juce::Font(17.0f, juce::Font::bold));
    g.drawText(juce::String::fromUTF8("Recherche de doublons"), 20, 14, 500, 22,
               juce::Justification::centredLeft);

    int files = 0;
    int64_t waste = 0;
    for (const auto& g2 : m_groups)
    {
        files += (int) g2.entries.size() - 1;
        waste += g2.wastedBytes;
    }
    g.setColour(Colors::textSecondary());
    g.setFont(juce::Font(12.0f));
    juce::String sub;
    if (m_groups.empty() && ! m_running.load())
        sub = juce::String::fromUTF8("Aucun doublon d\xc3\xa9tect\xc3\xa9 avec les crit\xc3\xa8res coch\xc3\xa9s.");
    else
        sub << (int) m_groups.size() << juce::String::fromUTF8(" groupe(s)  \xc2\xb7  ")
            << files << juce::String::fromUTF8(" fichier(s) en trop  \xc2\xb7  ")
            << fmtSize(waste) << juce::String::fromUTF8(" r\xc3\xa9""cup\xc3\xa9rables  \xc2\xb7  ")
            << checkedCount() << juce::String::fromUTF8(" coch\xc3\xa9(s)");
    g.drawText(sub, 20, 38, getWidth() - 40, 16, juce::Justification::centredLeft, true);

    if (m_running.load())
    {
        const int total = juce::jmax(1, m_total.load());
        const double pct = juce::jlimit(0.0, 1.0, (double) m_done.load() / total);
        auto bar = juce::Rectangle<float>(20.0f, (float) getHeight() - 92.0f,
                                          (float) getWidth() - 40.0f, 6.0f);
        g.setColour(Colors::bgLighter());
        g.fillRoundedRectangle(bar, 3.0f);
        g.setColour(Colors::primary());
        g.fillRoundedRectangle(bar.withWidth((float) (bar.getWidth() * pct)), 3.0f);
        g.setColour(Colors::textSecondary());
        g.setFont(juce::Font(11.5f));
        g.drawText(m_phase + "  " + juce::String(m_done.load()) + " / " + juce::String(total),
                   20, getHeight() - 84, getWidth() - 40, 16, juce::Justification::centredLeft, true);
    }
}

void DuplicateManagerDialog::resized()
{
    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(46);

    auto crit = area.removeFromTop(26);
    for (auto* t : { m_cFile.get(), m_cAudio.get(), m_cMeta.get(), m_cName.get() })
        if (t) { t->setBounds(crit.removeFromLeft(150)); crit.removeFromLeft(4); }
    if (m_cIgnoreTags) m_cIgnoreTags->setBounds(crit.removeFromLeft(320));

    area.removeFromTop(6);
    auto opts = area.removeFromTop(28);
    if (m_scanBtn) { m_scanBtn->setBounds(opts.removeFromLeft(120).reduced(0, 1)); opts.removeFromLeft(10); }
    if (m_keepCombo) { m_keepCombo->setBounds(opts.removeFromLeft(250).reduced(0, 1)); opts.removeFromLeft(10); }
    if (m_uncheckAllBtn) { m_uncheckAllBtn->setBounds(opts.removeFromRight(120).reduced(0, 1)); opts.removeFromRight(6); }
    if (m_checkAllBtn) m_checkAllBtn->setBounds(opts.removeFromRight(110).reduced(0, 1));

    area.removeFromTop(8);
    auto bottom = area.removeFromBottom(44);
    area.removeFromBottom(34);
    if (m_list) m_list->setBounds(area);

    if (m_removeBtn) { m_removeBtn->setBounds(bottom.removeFromLeft(230).reduced(0, 5)); bottom.removeFromLeft(8); }
    if (m_deleteBtn) { m_deleteBtn->setBounds(bottom.removeFromLeft(210).reduced(0, 5)); bottom.removeFromLeft(8); }
    if (m_revealBtn) { m_revealBtn->setBounds(bottom.removeFromLeft(150).reduced(0, 5)); bottom.removeFromLeft(8); }
    if (m_csvBtn) m_csvBtn->setBounds(bottom.removeFromLeft(130).reduced(0, 5));
    if (m_closeBtn) m_closeBtn->setBounds(bottom.removeFromRight(110).reduced(0, 5));
}

} // namespace BeatMate::UI
