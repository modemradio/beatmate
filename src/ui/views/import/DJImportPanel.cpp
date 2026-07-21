#include "DJImportPanel.h"
#include "../../styles/ColorPalette.h"
#include "../../widgets/ToastNotifier.h"
#include "../../../services/config/I18n.h"
#include "../../../app/ServiceLocator.h"
#include <spdlog/spdlog.h>

namespace BeatMate { extern ServiceLocator* g_serviceLocator; }

namespace BeatMate::UI {

namespace {
Services::DJSoftware::DJImportService* importService()
{
    if (!BeatMate::g_serviceLocator)
        return nullptr;
    return BeatMate::g_serviceLocator->tryGet<Services::DJSoftware::DJImportService>();
}
}

DJImportPanel::DJImportPanel()
{
    m_sourceGrid = std::make_unique<DJSourceGrid>();
    m_sourceGrid->onSourceChosen = [this](Services::DJSoftware::DJSoftwareType type) {
        chooseSource(type);
    };
    addAndMakeVisible(*m_sourceGrid);

    m_playlistPicker = std::make_unique<DJPlaylistPicker>();
    m_playlistPicker->onSelectionChanged = [this] {
        if (m_step == Step::PickPlaylists)
            m_importBtn->setEnabled(m_playlistPicker->wholeCollection()
                                    || !m_playlistPicker->selectedIds().empty());
    };
    addChildComponent(*m_playlistPicker);

    m_summary = std::make_unique<ImportSummaryCard>();
    m_summary->onViewLibrary = [this] { if (onNavigateToLibrary) onNavigateToLibrary(); };
    addChildComponent(*m_summary);

    auto makeBtn = [this](const juce::String& t, juce::Colour bg) {
        auto b = std::make_unique<juce::TextButton>(t);
        b->setColour(juce::TextButton::buttonColourId, bg);
        b->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        addChildComponent(*b);
        return b;
    };

    m_backBtn = makeBtn(BM_TJ("import.dj.back"), Colors::bgLighter());
    m_backBtn->onClick = [this] { showStep(Step::PickSource); };

    m_importBtn = makeBtn(BM_TJ("import.dj.importBtn"), Colors::primary());
    m_importBtn->onClick = [this] {
        m_syncRetries = 0;
        startImport();
    };

    m_cancelBtn = makeBtn(BM_TJ("import.btn.cancel"), Colors::error().withAlpha(0.3f));
    m_cancelBtn->onClick = [this] {
        if (m_retryWaiting)
        {
            m_retryWaiting = false;
            m_syncRetries = 0;
            showStep(Step::PickPlaylists);
            return;
        }
        if (auto* svc = importService())
            svc->cancel();
    };

    m_statusLabel = std::make_unique<juce::Label>("st", "");
    m_statusLabel->setFont(juce::Font(11.5f));
    m_statusLabel->setColour(juce::Label::textColourId, Colors::textSecondary());
    addAndMakeVisible(*m_statusLabel);

    showStep(Step::PickSource);
}

DJImportPanel::~DJImportPanel()
{
    m_alive->store(false);
    if (auto* svc = importService())
        svc->cancel();
    if (m_worker.joinable())
        m_worker.join();
}

void DJImportPanel::refreshSources()
{
    if (m_step != Step::PickSource)
        return;
    if (auto* svc = importService())
        m_sourceGrid->setSources(svc->detectSources());
}

void DJImportPanel::showStep(Step step)
{
    m_step = step;
    m_sourceGrid->setVisible(step == Step::PickSource);
    m_playlistPicker->setVisible(step == Step::PickPlaylists);
    m_summary->setVisible(step == Step::Done);
    m_backBtn->setVisible(step == Step::PickPlaylists || step == Step::Done);
    m_importBtn->setVisible(step == Step::PickPlaylists);
    m_cancelBtn->setVisible(step == Step::Importing);
    if (step == Step::PickSource)
    {
        m_statusLabel->setText(BM_TJ("import.dj.subtitle"), juce::dontSendNotification);
        refreshSources();
    }
    resized();
    repaint();
}

void DJImportPanel::chooseSource(Services::DJSoftware::DJSoftwareType type)
{
    if (m_loadingPlaylists)
        return;
    m_sourceType = type;
    m_sourceName = juce::String::fromUTF8(
        Services::DJSoftware::DJSoftwareManager::softwareTypeName(type).c_str());
    m_loadingPlaylists = true;
    m_statusLabel->setText(BM_TJ("import.dj.loadingPlaylists"), juce::dontSendNotification);

    auto alive = m_alive;
    juce::Component::SafePointer<DJImportPanel> self(this);
    if (m_worker.joinable())
        m_worker.join();
    m_worker = std::thread([self, alive, type]() {
        std::vector<Services::DJSoftware::ExternalPlaylistDescriptor> playlists;
        if (auto* svc = importService())
            playlists = svc->listPlaylists(type);
        juce::MessageManager::callAsync([self, alive, playlists = std::move(playlists)]() {
            if (!alive->load()) return;
            auto* panel = self.getComponent();
            if (!panel) return;
            panel->m_loadingPlaylists = false;
            panel->m_playlistPicker->setPlaylists(playlists);
            panel->m_importBtn->setEnabled(false);
            panel->showStep(Step::PickPlaylists);
        });
    });
}

void DJImportPanel::startImport()
{
    Services::DJSoftware::DJImportSelection selection;
    selection.type = m_sourceType;
    selection.wholeCollection = m_playlistPicker->wholeCollection();
    selection.playlistExternalIds = m_playlistPicker->selectedIds();
    selection.importPlaylists = true;

    if (!selection.wholeCollection && selection.playlistExternalIds.empty())
        return;

    m_progress = 0.0;
    m_statusLabel->setText(BM_TJ("import.dj.importing"), juce::dontSendNotification);
    showStep(Step::Importing);

    auto alive = m_alive;
    juce::Component::SafePointer<DJImportPanel> self(this);

    auto progressCb = [self, alive](const Services::DJSoftware::SyncProgress& p) {
        const int processed = p.processed;
        const int total = p.total;
        const juce::String item = juce::String::fromUTF8(p.currentItem.c_str());
        juce::MessageManager::callAsync([self, alive, processed, total, item]() {
            if (!alive->load()) return;
            if (auto* panel = self.getComponent())
                panel->applyProgress(processed, total, item);
        });
    };

    if (m_worker.joinable())
        m_worker.join();
    m_worker = std::thread([self, alive, selection, progressCb]() {
        Services::DJSoftware::DJImportReport report;
        if (auto* svc = importService())
            report = svc->importSelection(selection, progressCb);
        else
            report.error = "service unavailable";
        juce::MessageManager::callAsync([self, alive, report]() {
            if (!alive->load()) return;
            if (auto* panel = self.getComponent())
                panel->finishImport(report);
        });
    });
}

void DJImportPanel::applyProgress(int processed, int total, const juce::String& item)
{
    if (m_step != Step::Importing)
        return;
    m_progress = total > 0 ? static_cast<double>(processed) / total : 0.0;
    juce::String status = BM_TJ("import.dj.importing");
    if (item.isNotEmpty())
        status += "  " + item;
    m_statusLabel->setText(status, juce::dontSendNotification);
    repaint();
}

void DJImportPanel::finishImport(const Services::DJSoftware::DJImportReport& report)
{
    if (!report.error.empty())
    {
        if (report.error == "sync busy" && m_syncRetries < 15)
        {
            ++m_syncRetries;
            m_retryWaiting = true;
            m_statusLabel->setText(BM_TJ("import.dj.syncBusy"), juce::dontSendNotification);
            auto alive = m_alive;
            juce::Component::SafePointer<DJImportPanel> self(this);
            juce::Timer::callAfterDelay(2000, [self, alive]() {
                if (!alive->load()) return;
                auto* panel = self.getComponent();
                if (!panel || !panel->m_retryWaiting) return;
                panel->m_retryWaiting = false;
                panel->startImport();
            });
            return;
        }
        m_syncRetries = 0;
        const juce::String msg = report.error == "empty selection"
                                     ? BM_TJ("import.dj.emptySelection")
                                     : BM_TJ("import.dj.syncBusy");
        m_statusLabel->setText(msg, juce::dontSendNotification);
        Widgets::ToastNotifier::getInstance().show(
            msg, {}, Widgets::ToastNotifier::Kind::Warning, 6000);
        showStep(Step::PickPlaylists);
        return;
    }

    m_syncRetries = 0;
    std::vector<ImportSummaryCard::Counter> counters;
    counters.push_back({ BM_TJ("import.dj.tracksImported"), report.tracksImported, Colors::success() });
    counters.push_back({ BM_TJ("import.dj.playlistsImported"), report.playlistsImported, Colors::primary() });

    std::vector<ImportSummaryCard::ErrorLine> errors;
    m_summary->showSummary(std::move(counters), std::move(errors), false);
    m_statusLabel->setText(report.cancelled ? BM_TJ("import.dj.cancelled")
                                            : BM_TJ("import.status.ready"),
                           juce::dontSendNotification);
    Widgets::ToastNotifier::getInstance().show(
        juce::String(report.tracksImported) + " " + BM_TJ("import.dj.tracksImported"),
        juce::String(report.playlistsImported) + " " + BM_TJ("import.dj.playlistsImported"),
        report.cancelled ? Widgets::ToastNotifier::Kind::Warning
                         : Widgets::ToastNotifier::Kind::Success, 6000);
    showStep(Step::Done);
}

void DJImportPanel::paint(juce::Graphics& g)
{
    g.setColour(Colors::primary());
    g.fillRoundedRectangle(0.0f, 2.0f, 4.0f, 16.0f, 2.0f);
    g.setFont(juce::Font("Segoe UI", 14.0f, juce::Font::bold));
    g.setColour(Colors::textPrimary());
    juce::String title = BM_TJ("import.dj.title");
    if (m_step != Step::PickSource && m_sourceName.isNotEmpty())
        title += "  \xc2\xb7  " + m_sourceName;
    g.drawText(title, 12, 0, getWidth() - 24, 20, juce::Justification::centredLeft, true);

    if (m_step == Step::Importing)
    {
        const float pbX = 12.0f;
        const float pbY = static_cast<float>(getHeight()) / 2.0f;
        const float pbW = static_cast<float>(getWidth()) - 24.0f;
        g.setColour(Colors::bgLighter().withAlpha(0.5f));
        g.fillRoundedRectangle(pbX, pbY, pbW, 14.0f, 7.0f);
        if (m_progress > 0)
        {
            juce::ColourGradient grad(Colors::primary(), pbX, pbY,
                                      Colors::success(), pbX + pbW, pbY, false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(pbX, pbY, pbW * static_cast<float>(m_progress), 14.0f, 7.0f);
        }
    }
}

void DJImportPanel::resized()
{
    const int w = getWidth();
    const int h = getHeight();
    const int contentY = 30;

    m_sourceGrid->setBounds(0, contentY, w, h - contentY - 30);
    m_playlistPicker->setBounds(0, contentY, w, h - contentY - 46);

    if (m_step == Step::Done)
        m_summary->setBounds(w / 2 - 210, contentY + 20, 420, juce::jmin(240, h - contentY - 90));

    const int footY = h - 36;
    m_backBtn->setBounds(0, footY, 120, 28);
    m_importBtn->setBounds(w - 170, footY, 170, 28);
    m_cancelBtn->setBounds(w - 130, footY, 130, 28);
    m_statusLabel->setBounds(130, footY + 4, w - 320, 20);
}

void DJImportPanel::retranslateUi()
{
    m_backBtn->setButtonText(BM_TJ("import.dj.back"));
    m_importBtn->setButtonText(BM_TJ("import.dj.importBtn"));
    m_cancelBtn->setButtonText(BM_TJ("import.btn.cancel"));
    m_sourceGrid->retranslateUi();
    m_playlistPicker->retranslateUi();
    m_summary->retranslateUi();
    if (m_step == Step::PickSource)
        m_statusLabel->setText(BM_TJ("import.dj.subtitle"), juce::dontSendNotification);
    repaint();
}

} // namespace BeatMate::UI
