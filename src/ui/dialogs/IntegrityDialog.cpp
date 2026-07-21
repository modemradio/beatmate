#include "IntegrityDialog.h"
#include "../styles/ColorPalette.h"
#include "../widgets/ToastNotifier.h"

namespace BeatMate::UI {

namespace {

juce::Colour statusColour(IntegrityDialog::Status s)
{
    using S = IntegrityDialog::Status;
    switch (s)
    {
        case S::Ok:         return juce::Colour(0xff10b981);
        case S::Warning:    return juce::Colour(0xfff59e0b);
        case S::Corrupt:    return juce::Colour(0xffef4444);
        case S::Unreadable: return juce::Colour(0xff9ca3af);
        case S::Repaired:   return juce::Colour(0xff3b82f6);
        default:            return juce::Colour(0xff6b7280);
    }
}

bool isBad(IntegrityDialog::Status s)
{
    using S = IntegrityDialog::Status;
    return s == S::Corrupt || s == S::Unreadable;
}

} // namespace

void IntegrityDialog::show(std::vector<Entry> entries, const juce::String& scopeLabel,
                           std::function<void()> onFinished)
{
    if (! Services::Analysis::AudioIntegrityChecker::isAvailable())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            juce::String::fromUTF8("Contr\xc3\xb4le d'int\xc3\xa9grit\xc3\xa9"),
            juce::String::fromUTF8("L'outil d'analyse audio est introuvable. R\xc3\xa9installez BeatMate."));
        return;
    }
    if (entries.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            juce::String::fromUTF8("Contr\xc3\xb4le d'int\xc3\xa9grit\xc3\xa9"),
            juce::String::fromUTF8("Aucun morceau \xc3\xa0 v\xc3\xa9rifier."));
        return;
    }

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(new IntegrityDialog(std::move(entries), scopeLabel, std::move(onFinished)));
    opts.dialogTitle = juce::String::fromUTF8("Int\xc3\xa9grit\xc3\xa9 des fichiers \xe2\x80\x94 BeatMate");
    opts.dialogBackgroundColour = Colors::bgDark();
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = true;
    opts.launchAsync();
}

IntegrityDialog::IntegrityDialog(std::vector<Entry> entries, const juce::String& scopeLabel,
                                 std::function<void()> onFinished)
    : m_all(std::move(entries)), m_scope(scopeLabel), m_onFinished(std::move(onFinished))
{
    for (auto& e : m_all)
    {
        const auto known = m_checker.statusFor(e.path);
        e.status = known.status;
        e.details = known.details;
    }

    auto mkBtn = [this](std::unique_ptr<juce::TextButton>& b, const juce::String& t,
                        juce::Colour c, std::function<void()> fn) {
        b = std::make_unique<juce::TextButton>(t);
        b->setColour(juce::TextButton::buttonColourId, c);
        b->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        b->onClick = std::move(fn);
        addAndMakeVisible(*b);
    };

    mkBtn(m_checkBtn, juce::String::fromUTF8("V\xc3\xa9rifier"), Colors::primary(),
          [this] { if (m_running.load()) cancelWork(); else startCheck(false); });
    mkBtn(m_repairBtn, juce::String::fromUTF8("R\xc3\xa9parer"), Colors::error().darker(0.2f),
          [this] { startRepair(); });
    mkBtn(m_revealBtn, juce::String::fromUTF8("Ouvrir le dossier"), Colors::bgLighter(),
          [this] {
              const int r = m_list ? m_list->getSelectedRow() : -1;
              if (r >= 0 && r < (int) m_filtered.size())
                  juce::File(m_all[(size_t) m_filtered[(size_t) r]].path).revealToUser();
          });
    mkBtn(m_csvBtn, juce::String::fromUTF8("Rapport CSV"), Colors::bgLighter(),
          [this] { exportCsv(); });
    mkBtn(m_allBtn, juce::String::fromUTF8("Tout cocher"), Colors::bgLighter(),
          [this] { for (auto& e : m_all) e.checked = true; if (m_list) m_list->repaint(); updateButtons(); });
    mkBtn(m_noneBtn, juce::String::fromUTF8("Tout d\xc3\xa9""cocher"), Colors::bgLighter(),
          [this] { for (auto& e : m_all) e.checked = false; if (m_list) m_list->repaint(); updateButtons(); });
    mkBtn(m_badOnlyBtn, juce::String::fromUTF8("Cocher les corrompus"), Colors::error().darker(0.45f),
          [this] {
              for (auto& e : m_all) e.checked = isBad(e.status);
              if (m_list) m_list->repaint();
              updateButtons();
          });
    mkBtn(m_closeBtn, juce::String::fromUTF8("Fermer"), Colors::bgLighter(),
          [this] {
              cancelWork();
              if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(0);
          });

    auto mkFilter = [this](std::unique_ptr<juce::ToggleButton>& t, const juce::String& text, bool on) {
        t = std::make_unique<juce::ToggleButton>(text);
        t->setToggleState(on, juce::dontSendNotification);
        t->setColour(juce::ToggleButton::textColourId, Colors::textSecondary());
        t->setColour(juce::ToggleButton::tickColourId, Colors::primary());
        t->onClick = [this] { rebuildFiltered(); };
        addAndMakeVisible(*t);
    };
    mkFilter(m_fBad, juce::String::fromUTF8("Corrompus"), true);
    mkFilter(m_fWarn, juce::String::fromUTF8("Suspects"), true);
    mkFilter(m_fUnknown, juce::String::fromUTF8("Non v\xc3\xa9rifi\xc3\xa9s"), true);
    mkFilter(m_fOk, juce::String::fromUTF8("Intacts"), true);

    m_list = std::make_unique<juce::ListBox>("integrity", this);
    m_list->setRowHeight(44);
    m_list->setColour(juce::ListBox::backgroundColourId, Colors::bgDark());
    addAndMakeVisible(*m_list);

    rebuildFiltered();
    updateButtons();
    setSize(980, 620);

    juce::Component::SafePointer<IntegrityDialog> safe(this);
    juce::MessageManager::callAsync([safe] { if (safe != nullptr) safe->startCheck(true); });
}

void IntegrityDialog::focusOnProblems()
{
    int bad = 0, warn = 0;
    for (const auto& e : m_all) { if (isBad(e.status)) ++bad; else if (e.status == Status::Warning) ++warn; }
    if (bad + warn == 0) return;

    if (m_fOk) m_fOk->setToggleState(false, juce::dontSendNotification);
    if (m_fUnknown) m_fUnknown->setToggleState(false, juce::dontSendNotification);
    if (m_fBad) m_fBad->setToggleState(true, juce::dontSendNotification);
    if (m_fWarn) m_fWarn->setToggleState(true, juce::dontSendNotification);
    for (auto& e : m_all) e.checked = isBad(e.status);
    rebuildFiltered();
    updateButtons();
}

IntegrityDialog::~IntegrityDialog()
{
    cancelWork();
    if (m_worker.joinable()) m_worker.join();
}

void IntegrityDialog::cancelWork()
{
    m_cancel.store(true);
    stopTimer();
}

std::vector<int> IntegrityDialog::checkedIndices() const
{
    std::vector<int> out;
    for (int i = 0; i < (int) m_all.size(); ++i)
        if (m_all[(size_t) i].checked) out.push_back(i);
    return out;
}

void IntegrityDialog::rebuildFiltered()
{
    m_filtered.clear();
    for (int i = 0; i < (int) m_all.size(); ++i)
    {
        const auto s = m_all[(size_t) i].status;
        const bool keep =
            (isBad(s)              && m_fBad && m_fBad->getToggleState()) ||
            (s == Status::Warning  && m_fWarn && m_fWarn->getToggleState()) ||
            (s == Status::Unknown  && m_fUnknown && m_fUnknown->getToggleState()) ||
            ((s == Status::Ok || s == Status::Repaired) && m_fOk && m_fOk->getToggleState());
        if (keep) m_filtered.push_back(i);
    }
    if (m_list) m_list->updateContent();
    repaint();
}

void IntegrityDialog::updateButtons()
{
    int bad = 0;
    for (const auto& e : m_all) if (isBad(e.status) && e.checked) ++bad;
    if (m_repairBtn)
    {
        m_repairBtn->setButtonText(bad > 0
            ? juce::String::fromUTF8("R\xc3\xa9parer (") + juce::String(bad) + ")"
            : juce::String::fromUTF8("R\xc3\xa9parer"));
        m_repairBtn->setEnabled(bad > 0 && ! m_running.load());
    }
    if (m_checkBtn)
        m_checkBtn->setButtonText(m_running.load()
            ? juce::String::fromUTF8("Arr\xc3\xaater")
            : juce::String::fromUTF8("V\xc3\xa9rifier"));
}

void IntegrityDialog::startCheck(bool onlyUnknown)
{
    if (m_running.load()) return;
    if (m_worker.joinable()) m_worker.join();

    auto idx = checkedIndices();
    if (onlyUnknown)
    {
        std::vector<int> f;
        for (int i : idx) if (m_all[(size_t) i].status == Status::Unknown) f.push_back(i);
        idx = f;
    }
    if (idx.empty())
    {
        if (onlyUnknown) { focusOnProblems(); return; }
        Widgets::ToastNotifier::getInstance().show(
            juce::String::fromUTF8("Int\xc3\xa9grit\xc3\xa9"),
            juce::String::fromUTF8("Cochez au moins un morceau."),
            Widgets::ToastNotifier::Kind::Info, 4000);
        return;
    }

    m_running.store(true);
    m_cancel.store(false);
    m_done.store(0);
    m_total.store((int) idx.size());
    m_phase = juce::String::fromUTF8("V\xc3\xa9rification");
    updateButtons();
    startTimerHz(8);

    juce::Component::SafePointer<IntegrityDialog> safe(this);
    m_worker = std::thread([this, safe, idx]
    {
        for (int i : idx)
        {
            if (m_cancel.load()) break;
            const juce::String path = m_all[(size_t) i].path;
            const auto rep = m_checker.check(path);
            juce::MessageManager::callAsync([safe, i, rep, path]
            {
                if (safe == nullptr) return;
                safe->m_all[(size_t) i].status = rep.status;
                safe->m_all[(size_t) i].details = rep.details;
                safe->m_currentFile = juce::File(path).getFileName();
            });
            m_done.fetch_add(1);
        }
        juce::MessageManager::callAsync([safe]
        {
            if (safe == nullptr) return;
            safe->m_running.store(false);
            safe->stopTimer();
            safe->focusOnProblems();
            safe->rebuildFiltered();
            safe->updateButtons();
            int bad = 0, warn = 0;
            for (const auto& e : safe->m_all) { if (isBad(e.status)) ++bad; else if (e.status == Status::Warning) ++warn; }
            Widgets::ToastNotifier::getInstance().show(
                juce::String::fromUTF8("V\xc3\xa9rification termin\xc3\xa9""e"),
                juce::String(bad) + juce::String::fromUTF8(" corrompu(s), ")
                    + juce::String(warn) + juce::String::fromUTF8(" suspect(s)"),
                bad > 0 ? Widgets::ToastNotifier::Kind::Warning : Widgets::ToastNotifier::Kind::Success, 6000);
            if (safe->m_onFinished) safe->m_onFinished();
        });
    });
}

void IntegrityDialog::startRepair()
{
    if (m_running.load()) return;
    std::vector<int> idx;
    for (int i : checkedIndices()) if (isBad(m_all[(size_t) i].status)) idx.push_back(i);
    if (idx.empty()) return;

    juce::Component::SafePointer<IntegrityDialog> safe(this);
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon,
        juce::String::fromUTF8("R\xc3\xa9parer les fichiers"),
        juce::String((int) idx.size())
            + juce::String::fromUTF8(" fichier(s) seront r\xc3\xa9par\xc3\xa9s sans perte. "
                                     "Chaque original est copi\xc3\xa9 dans integrity_backups avant modification. Continuer ?"),
        juce::String::fromUTF8("R\xc3\xa9parer"), juce::String::fromUTF8("Annuler"), nullptr,
        juce::ModalCallbackFunction::create([safe, idx](int res)
        {
            if (res != 1 || safe == nullptr) return;
            if (safe->m_worker.joinable()) safe->m_worker.join();
            safe->m_running.store(true);
            safe->m_cancel.store(false);
            safe->m_done.store(0);
            safe->m_total.store((int) idx.size());
            safe->m_phase = juce::String::fromUTF8("R\xc3\xa9paration");
            safe->updateButtons();
            safe->startTimerHz(8);

            safe->m_worker = std::thread([safe, idx]
            {
                int okCount = 0, failCount = 0;
                for (int i : idx)
                {
                    if (safe == nullptr || safe->m_cancel.load()) break;
                    const juce::String path = safe->m_all[(size_t) i].path;
                    const auto rep = safe->m_checker.repair(path);
                    (rep.status == Status::Repaired || rep.status == Status::Ok) ? ++okCount : ++failCount;
                    juce::MessageManager::callAsync([safe, i, rep, path]
                    {
                        if (safe == nullptr) return;
                        safe->m_all[(size_t) i].status = rep.status;
                        safe->m_all[(size_t) i].details = rep.details;
                        safe->m_currentFile = juce::File(path).getFileName();
                    });
                    safe->m_done.fetch_add(1);
                }
                juce::MessageManager::callAsync([safe, okCount, failCount]
                {
                    if (safe == nullptr) return;
                    safe->m_running.store(false);
                    safe->stopTimer();
                    safe->rebuildFiltered();
                    safe->updateButtons();
                    juce::String msg;
                    msg << okCount << juce::String::fromUTF8(" fichier(s) r\xc3\xa9par\xc3\xa9(s)");
                    if (failCount > 0) msg << ", " << failCount << juce::String::fromUTF8(" \xc3\xa9""chec(s)");
                    Widgets::ToastNotifier::getInstance().show(
                        juce::String::fromUTF8("R\xc3\xa9paration termin\xc3\xa9""e"), msg,
                        failCount > 0 ? Widgets::ToastNotifier::Kind::Warning
                                      : Widgets::ToastNotifier::Kind::Success, 8000);
                    if (safe->m_onFinished) safe->m_onFinished();
                });
            });
        }));
}

void IntegrityDialog::exportCsv()
{
    m_chooser = std::make_unique<juce::FileChooser>(
        juce::String::fromUTF8("Exporter le rapport d'int\xc3\xa9grit\xc3\xa9"),
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
            .getChildFile("integrite-beatmate.csv"), "*.csv");
    auto rows = m_all;
    m_chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [rows](const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            juce::String csv;
            csv << juce::String::fromUTF8("Statut;Titre;Fichier;D\xc3\xa9tails\r\n");
            for (const auto& e : rows)
            {
                auto field = [](juce::String s) {
                    s = s.replace("\r", " ").replace("\n", " ");
                    return s.containsAnyOf(";\"") ? "\"" + s.replace("\"", "\"\"") + "\"" : s;
                };
                csv << field(Services::Analysis::AudioIntegrityChecker::statusLabel(e.status)) << ";"
                    << field(e.title) << ";" << field(e.path) << ";" << field(e.details) << "\r\n";
            }
            f.replaceWithText("\xEF\xBB\xBF" + csv);
            f.revealToUser();
        });
}

void IntegrityDialog::timerCallback() { repaint(); }

int IntegrityDialog::getNumRows() { return (int) m_filtered.size(); }

void IntegrityDialog::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) m_filtered.size()) return;
    const auto& e = m_all[(size_t) m_filtered[(size_t) row]];

    if (selected) { g.setColour(Colors::primary().withAlpha(0.16f)); g.fillRect(0, 0, w, h); }
    else if (row % 2 == 1) { g.setColour(juce::Colours::white.withAlpha(0.022f)); g.fillRect(0, 0, w, h); }

    juce::Rectangle<float> box(10.0f, h * 0.5f - 8.0f, 16.0f, 16.0f);
    g.setColour(e.checked ? Colors::primary() : Colors::bgLighter());
    g.fillRoundedRectangle(box, 3.0f);
    g.setColour(Colors::border());
    g.drawRoundedRectangle(box, 3.0f, 1.0f);
    if (e.checked)
    {
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText("X", box.toNearestInt(), juce::Justification::centred);
    }

    const auto col = statusColour(e.status);
    g.setColour(col.withAlpha(0.18f));
    g.fillRoundedRectangle(36.0f, 6.0f, 92.0f, (float) h - 12.0f, 9.0f);
    g.setColour(col);
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.drawText(Services::Analysis::AudioIntegrityChecker::statusLabel(e.status),
               36, 6, 92, h - 12, juce::Justification::centred);

    g.setColour(Colors::textPrimary());
    g.setFont(juce::Font(12.5f, juce::Font::bold));
    g.drawText(e.title.isNotEmpty() ? e.title : juce::File(e.path).getFileName(),
               138, 4, w - 150, 17, juce::Justification::centredLeft, true);

    g.setColour(Colors::textMuted());
    g.setFont(juce::Font(10.5f));
    juce::String sub = e.details.upToFirstOccurrenceOf("\n", false, false);
    if (sub.isEmpty()) sub = e.path;
    g.drawText(sub, 138, 22, w - 150, 16, juce::Justification::centredLeft, true);
}

void IntegrityDialog::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) m_filtered.size()) return;
    if (e.x < 32)
    {
        auto& item = m_all[(size_t) m_filtered[(size_t) row]];
        item.checked = ! item.checked;
        m_list->repaintRow(row);
        updateButtons();
    }
}

void IntegrityDialog::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= (int) m_filtered.size()) return;
    juce::File(m_all[(size_t) m_filtered[(size_t) row]].path).revealToUser();
}

void IntegrityDialog::paint(juce::Graphics& g)
{
    g.fillAll(Colors::bgDark());

    g.setColour(Colors::textPrimary());
    g.setFont(juce::Font(17.0f, juce::Font::bold));
    g.drawText(juce::String::fromUTF8("Int\xc3\xa9grit\xc3\xa9 des fichiers"), 20, 14, 500, 22,
               juce::Justification::centredLeft);

    int ok = 0, warn = 0, bad = 0, unknown = 0;
    for (const auto& e : m_all)
    {
        if (isBad(e.status)) ++bad;
        else if (e.status == Status::Warning) ++warn;
        else if (e.status == Status::Unknown) ++unknown;
        else ++ok;
    }
    g.setColour(Colors::textSecondary());
    g.setFont(juce::Font(12.0f));
    juce::String sub;
    sub << m_scope << juce::String::fromUTF8("  \xc2\xb7  ") << (int) m_all.size()
        << juce::String::fromUTF8(" morceau(x)  \xc2\xb7  ") << ok << juce::String::fromUTF8(" intacts, ")
        << warn << juce::String::fromUTF8(" suspects, ") << bad << juce::String::fromUTF8(" corrompus, ")
        << unknown << juce::String::fromUTF8(" non v\xc3\xa9rifi\xc3\xa9s");
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
        g.drawText(m_phase + "  " + juce::String(m_done.load()) + " / " + juce::String(total)
                       + (m_currentFile.isNotEmpty() ? juce::String::fromUTF8("  \xc2\xb7  ") + m_currentFile : juce::String()),
                   20, getHeight() - 84, getWidth() - 40, 16, juce::Justification::centredLeft, true);
    }
}

void IntegrityDialog::resized()
{
    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(46);

    auto filters = area.removeFromTop(28);
    for (auto* t : { m_fBad.get(), m_fWarn.get(), m_fUnknown.get(), m_fOk.get() })
        if (t) { t->setBounds(filters.removeFromLeft(112)); filters.removeFromLeft(4); }
    if (m_allBtn) { m_allBtn->setBounds(filters.removeFromRight(104).reduced(0, 1)); filters.removeFromRight(6); }
    if (m_noneBtn) { m_noneBtn->setBounds(filters.removeFromRight(114).reduced(0, 1)); filters.removeFromRight(6); }
    if (m_badOnlyBtn) m_badOnlyBtn->setBounds(filters.removeFromRight(160).reduced(0, 1));

    area.removeFromTop(8);
    auto bottom = area.removeFromBottom(44);
    area.removeFromBottom(34);
    if (m_list) m_list->setBounds(area);

    if (m_checkBtn)  { m_checkBtn->setBounds(bottom.removeFromLeft(120).reduced(0, 5)); bottom.removeFromLeft(8); }
    if (m_repairBtn) { m_repairBtn->setBounds(bottom.removeFromLeft(150).reduced(0, 5)); bottom.removeFromLeft(8); }
    if (m_revealBtn) { m_revealBtn->setBounds(bottom.removeFromLeft(150).reduced(0, 5)); bottom.removeFromLeft(8); }
    if (m_csvBtn)    m_csvBtn->setBounds(bottom.removeFromLeft(130).reduced(0, 5));
    if (m_closeBtn)  m_closeBtn->setBounds(bottom.removeFromRight(110).reduced(0, 5));
}

} // namespace BeatMate::UI
