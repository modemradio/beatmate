#include "MainWindow.h"
#include "views/HomeView.h"
#include "views/LibraryView.h"
#include "views/ImportView.h"
#include "views/AnalysisView.h"
#include "views/HotCueView.h"
#include "views/NormalizationView.h"
#include "views/SetPreparationView.h"
#include "views/SoireePreparationView.h"
#include "views/AgendaView.h"
#include "views/ComparisonView.h"
#include "views/PlaylistPreparationView.h"
#include "views/PreparationHubView.h"
#include "views/PreparationPlaylistView.h"
#include "dialogs/BeatMateLiveWindow.h"
#include "views/ExportView.h"
#include "widgets/utility/ModuleLoadSplash.h"
#include "views/StreamingView.h"
#include "views/SettingsView.h"
#include "views/HelpView.h"
#include "widgets/player/NowPlayingBar.h"
#include "styles/ColorPalette.h"
#include "../services/library/TrackDataProvider.h"
#include "../app/ServiceLocator.h"
#include "../services/preparation/EventPlanner.h"
#include "utils/ViewPrefs.h"
#include "widgets/ToastNotifier.h"
#include "../services/config/LocalizationService.h"
#include "IRetranslatable.h"
#include "../services/config/I18n.h"
#include "../core/audio/AudioEngine.h"
#include "../core/audio/AudioPlayer.h"
#include "../core/audio/AudioFileReader.h"
#include "../core/audio/StreamingPlayer.h"
#include "../core/analysis/AudioAnalysisPipeline.h"
#include "../services/analysis/AnalysisRunner.h"
#include "../services/export/MultiFormatExporter.h"
#include "../services/normalization/LoudnessNormalizer.h"
#include "../services/djsoftware/rekordbox/RekordboxXmlExporter.h"
#include "../services/djsoftware/traktor/TraktorNmlExporter.h"
#include "../services/djsoftware/virtualdj/VirtualDJExporter.h"
#include "../services/djsoftware/serato/SeratoTagWriter.h"
#include "../core/cue/CuePointGenerator.h"
#include "../services/export/PlaylistExportService.h"
#include "../models/CuePoint.h"
#include "../models/TrackAnalysis.h"
#include "../models/Track.h"
#include "../services/security/LicenseService.h"
#include "../services/library/TrackCacheService.h"
#include "../services/library/TrackMetadata.h"
#include "../services/library/TrackDatabase.h"
#include "../services/library/TrackRelinkService.h"
#include "../app/Application.h"
#include "../core/analysis/WaveformCacheService.h"
#include "../core/stems/StemSepSotaService.h"
#include "../services/config/SettingsManager.h"
#include "../services/library/WaveformPrecacheService.h"
#include "styles/Motion.h"
#include <cmath>
#include <thread>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <spdlog/spdlog.h>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

namespace BeatMate { extern ServiceLocator* g_serviceLocator; }

namespace BeatMate::UI {

using ::BeatMate::g_serviceLocator;

static juce::Colour moduleAccentFor(int navTarget)
{
    switch (navTarget)
    {
        case MainWindow::Nav_Home:               return Colors::moduleHome();
        case MainWindow::Nav_Library:            return Colors::moduleLibrary();
        case MainWindow::Nav_Import:             return Colors::moduleImport();
        case MainWindow::Nav_Analysis:           return Colors::moduleAnalysis();
        case MainWindow::Nav_HotCues:            return Colors::moduleHotcues();
        case MainWindow::Nav_Normalization:      return Colors::moduleNormalize();
        case MainWindow::Nav_SetPreparation:
        case MainWindow::Nav_SoireePreparation:
        case MainWindow::Nav_PlaylistPreparation: return Colors::modulePrep();
        case MainWindow::Nav_BeatMateLive:       return Colors::moduleLive();
        case MainWindow::Nav_Export:             return Colors::moduleExport();
        case MainWindow::Nav_Streaming:          return Colors::moduleStreaming();
        case MainWindow::Nav_SonicDeck:
        case MainWindow::Nav_Jingle:
        case MainWindow::Nav_Mix:
        case MainWindow::Nav_PerfDJ:             return Colors::moduleMixlab();
        case MainWindow::Nav_Settings:
        case MainWindow::Nav_Help:               return Colors::moduleSettings();
        default:                                 return Colors::primary();
    }
}

#if JUCE_WINDOWS
// Hack Windows : SetForegroundWindow seul ne suffit pas pour ramener la fenêtre devant.
static void bringWindowToForegroundHard(HWND hwnd)
{
    if (!hwnd) return;

    // 1. Fake an ALT keypress so our thread is considered "input-active".
    INPUT ip = {};
    ip.type = INPUT_KEYBOARD;
    ip.ki.wVk = VK_MENU;
    SendInput(1, &ip, sizeof(INPUT));
    ip.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &ip, sizeof(INPUT));

    // 2. Allow any foreground change (ASFW_ANY == (DWORD)-1).
    AllowSetForegroundWindow(ASFW_ANY);

    HWND fg = GetForegroundWindow();
    DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD ourThread = GetCurrentThreadId();
    if (fgThread && fgThread != ourThread)
        AttachThreadInput(fgThread, ourThread, TRUE);

    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);

    // Astuce non documentée mais fiable : minimiser puis restaurer force le focus.
    ShowWindow(hwnd, SW_MINIMIZE);
    ShowWindow(hwnd, SW_RESTORE);

    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    SetActiveWindow(hwnd);

    // 7. SwitchToThisWindow - undocumented, battle-tested final hammer.
    SwitchToThisWindow(hwnd, TRUE);

    if (fgThread && fgThread != ourThread)
        AttachThreadInput(fgThread, ourThread, FALSE);
}
#endif


MainWindow::MainWindow(const juce::String& name)
    : juce::DocumentWindow(name,
                           Colors::bg(),
                           juce::DocumentWindow::allButtons)
{
    spdlog::info("[MainWindow] outer ctor: start");
    setUsingNativeTitleBar(true);
    spdlog::info("[MainWindow] outer ctor: about to new ContentComponent");
    auto* content = new ContentComponent(*this);
    spdlog::info("[MainWindow] outer ctor: ContentComponent new OK");
    setContentOwned(content, true);
    spdlog::info("[MainWindow] outer ctor: setContentOwned OK");

    setResizable(true, true);
    // Plancher a 1280x720 : couvre les portables 1366x768 (la barre des taches
    // deduite, il reste ~720 de haut), jusqu'au 4K.
    setResizeLimits(1280, 720, 3840, 2160);

    auto display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    if (display)
        setBounds(display->userArea);
    else
        centreWithSize(1400, 900);
    spdlog::info("[MainWindow] outer ctor: bounds set");
    setFullScreen(true);
    spdlog::info("[MainWindow] outer ctor: fullscreen set");

    setVisible(true);
    spdlog::info("[MainWindow] outer ctor: visible");

    // Forcer le curseur normal : Windows peut laisser le sablier bloqué.
    juce::MouseCursor::hideWaitCursor();
    setMouseCursor(juce::MouseCursor::NormalCursor);
    if (auto* peer = getPeer()) {
        peer->getComponent().setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    // Forcer un WM_PAINT immédiat pour que la fenêtre ait un contenu visible.
    toFront(true);
    repaint();

    startTimer(2000);
    spdlog::info("[MainWindow] outer ctor: timer started, ctor END");
}

MainWindow::~MainWindow()
{
    stopTimer();
}

void MainWindow::closeButtonPressed()
{
    spdlog::warn("[MainWindow] closeButtonPressed — user clicked close");
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

void MainWindow::navigateTo(NavigationItem item)
{
    if (auto* content = dynamic_cast<ContentComponent*>(getContentComponent()))
    {
        content->navigateTo(static_cast<int>(item));
        m_listeners.call([item](Listener& l) { l.navigationChanged(static_cast<int>(item)); });
    }
}

void MainWindow::ContentComponent::updateAgendaReminder()
{
    // Throttle : un scan toutes les ~10 s suffit largement.
    const auto now = juce::Time::getMillisecondCounter();
    if (m_lastReminderCheckMs != 0 && now - m_lastReminderCheckMs < 10000)
        return;
    m_lastReminderCheckMs = now;
    if (! m_agendaReminderBtn) return;

    extern BeatMate::ServiceLocator* g_serviceLocator;
    auto* planner = g_serviceLocator
        ? g_serviceLocator->tryGet<Services::Preparation::EventPlanner>() : nullptr;

    auto parseCsvMinutes = [](const juce::String& csv) {
        std::vector<int64_t> out;
        for (const auto& part : juce::StringArray::fromTokens(csv, ",", ""))
            if (part.trim().getIntValue() > 0)
                out.push_back(part.trim().getIntValue());
        std::sort(out.begin(), out.end());
        return out;
    };

    // Rappels par défaut (réglage global de l'Agenda), avec migration douce
    std::vector<int64_t> defaultWindows;
    {
        juce::String csv = juce::String(Prefs::getString("agenda.reminders", ""));
        if (csv.isEmpty())
        {
            const int legacy = Prefs::getInt("agenda.reminderMinutes", 60);
            if (legacy > 0) csv = juce::String(legacy);
        }
        defaultWindows = parseCsvMinutes(csv);
    }

    const int tbY = 5, tbH = kToolbarHeight - 10;
    auto showIdle = [this, tbY, tbH] {
        // Cloche toujours visible, discrète quand aucun rappel imminent.
        m_agendaReminderBtn->setButtonText(juce::String::fromUTF8("\xf0\x9f\x94\x94"));
        m_agendaReminderBtn->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        m_agendaReminderBtn->setColour(juce::TextButton::textColourOffId, Colors::textMuted());
        m_agendaReminderBtn->setTooltip(juce::String::fromUTF8(
            "Aucun rappel imminent \xe2\x80\x94 cliquez pour ouvrir l'Agenda"));
        m_agendaReminderBtn->setBounds(getWidth() - 52, tbY, 40, tbH);
        m_agendaReminderBtn->setVisible(true);
    };

    if (! planner)
    {
        showIdle();
        return;
    }

    const int64_t nowUnix = (int64_t) (juce::Time::getCurrentTime().toMilliseconds() / 1000);
    const Services::Preparation::EventPlan* next = nullptr;
    std::vector<int64_t> windowsMin;
    auto acked = [this](int64_t key) {
        return std::find(m_ackedReminderKeys.begin(), m_ackedReminderKeys.end(), key)
               != m_ackedReminderKeys.end();
    };

    auto events = planner->getEvents();
    for (const auto& e : events)
    {
        if (e.status == "cancelled") continue;
        auto wins = e.reminders.empty() ? defaultWindows
                                        : parseCsvMinutes(juce::String::fromUTF8(e.reminders.c_str()));
        if (wins.empty()) continue;
        const int64_t delta = e.startTime - nowUnix;
        if (delta < -1800 || delta > wins.back() * 60) continue;   // passé >30 min ou hors fenêtres

        // Au moins un palier franchi et non encore acquitte par un clic.
        bool pending = false;
        for (int64_t w : wins)
            if (delta <= w * 60 && ! acked(e.id * 1000000 + w)) { pending = true; break; }
        if (! pending) continue;

        if (! next || e.startTime < next->startTime) { next = &e; windowsMin = wins; }
    }

    if (! next)
    {
        m_activeReminderKeys.clear();
        showIdle();
        return;
    }

    m_activeReminderKeys.clear();
    for (int64_t w : windowsMin)
        if (next->startTime - nowUnix <= w * 60)
            m_activeReminderKeys.push_back(next->id * 1000000 + w);
    m_agendaReminderBtn->setBounds(getWidth() - 292, tbY, 280, tbH);
    m_agendaReminderBtn->setTooltip(juce::String::fromUTF8(
        "Prestation imminente \xe2\x80\x94 cliquez pour ouvrir l'Agenda"));

    const int64_t delta = next->startTime - nowUnix;
    juce::Time st(next->startTime * (int64_t) 1000);
    juce::String when;
    if (delta <= 0)                    when = juce::String::fromUTF8("maintenant");
    else if (delta < 3600)             when = juce::String((int) ((delta + 59) / 60)) + " min";
    else if (delta < 48 * 3600)        when = st.formatted("%H:%M");
    else                               when = st.formatted("%d/%m");
    m_agendaReminderBtn->setButtonText(juce::String::fromUTF8("\xf0\x9f\x94\x94 ")
        + juce::String::fromUTF8(next->name.c_str()) + juce::String::fromUTF8("  \xc2\xb7  ") + when);
    const bool urgent = delta <= 900;   // < 15 min
    m_agendaReminderBtn->setColour(juce::TextButton::buttonColourId,
        (urgent ? Colors::error() : Colors::warning()).withAlpha(0.22f));
    m_agendaReminderBtn->setColour(juce::TextButton::textColourOffId,
        urgent ? Colors::error() : Colors::warning());
    m_agendaReminderBtn->setVisible(true);

    // Un toast par palier franchi, une seule fois par événement.
    for (int64_t w : windowsMin)
    {
        if (delta > w * 60) continue;                 // palier pas encore atteint
        const int64_t key = next->id * 1000000 + w;
        if (std::find(m_notifiedEventIds.begin(), m_notifiedEventIds.end(), key)
            != m_notifiedEventIds.end()) continue;
        m_notifiedEventIds.push_back(key);
        juce::String horizon;
        if (w >= 43200)      horizon = juce::String((int) (w / 43200)) + juce::String::fromUTF8(" mois");
        else if (w >= 10080) horizon = juce::String::fromUTF8("1 semaine");
        else if (w >= 1440)  horizon = juce::String((int) (w / 1440)) + juce::String::fromUTF8(" jour(s)");
        else if (w >= 60)    horizon = juce::String((int) (w / 60)) + " h";
        else                 horizon = juce::String((int) w) + " min";
        juce::LookAndFeel::getDefaultLookAndFeel().playAlertSound();
        Widgets::ToastNotifier::getInstance().show(
            juce::String::fromUTF8("\xf0\x9f\x94\x94 Prestation dans moins de ") + horizon,
            juce::String::fromUTF8(next->name.c_str())
                + (next->venue.empty() ? juce::String()
                   : juce::String::fromUTF8("  \xc2\xb7  ") + juce::String::fromUTF8(next->venue.c_str()))
                + juce::String::fromUTF8("  \xc2\xb7  ") + st.formatted("%d/%m %H:%M"),
            Widgets::ToastNotifier::Kind::Warning, 12000);
        break;   // un seul toast par passage
    }
}

void MainWindow::ContentComponent::acknowledgeReminders()
{
    for (int64_t k : m_activeReminderKeys)
        if (std::find(m_ackedReminderKeys.begin(), m_ackedReminderKeys.end(), k)
            == m_ackedReminderKeys.end())
            m_ackedReminderKeys.push_back(k);

    m_activeReminderKeys.clear();
    if (m_ackedReminderKeys.size() > 512)
        m_ackedReminderKeys.erase(m_ackedReminderKeys.begin(),
                                  m_ackedReminderKeys.begin() + 256);

    m_lastReminderCheckMs = 0;   // recalcul immediat au prochain tick
    updateAgendaReminder();
}

void MainWindow::timerCallback()
{
    // Masquer le curseur d'attente : sablier permanent sinon.
    juce::MouseCursor::hideWaitCursor();

    if (auto* content = dynamic_cast<ContentComponent*>(getContentComponent()))
    {
        content->updateAgendaReminder();

        double cpuPercent = 0.0;
        float ramUsedGB = 0.0f, ramTotalGB = 0.0f;
        float diskUsedGB = 0.0f, diskTotalGB = 0.0f;

#ifdef _WIN32
        static FILETIME prevIdle{}, prevKernel{}, prevUser{};
        FILETIME idle, kernel, user;
        if (GetSystemTimes(&idle, &kernel, &user))
        {
            auto ftToULL = [](const FILETIME& ft) {
                return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
            };
            ULONGLONG dIdle = ftToULL(idle) - ftToULL(prevIdle);
            ULONGLONG dKernel = ftToULL(kernel) - ftToULL(prevKernel);
            ULONGLONG dUser = ftToULL(user) - ftToULL(prevUser);
            ULONGLONG total = dKernel + dUser;
            if (total > 0)
                cpuPercent = (double)(total - dIdle) * 100.0 / total;
            prevIdle = idle; prevKernel = kernel; prevUser = user;
        }

        MEMORYSTATUSEX memStatus;
        memStatus.dwLength = sizeof(memStatus);
        if (GlobalMemoryStatusEx(&memStatus))
        {
            ramTotalGB = memStatus.ullTotalPhys / (1024.0f * 1024.0f * 1024.0f);
            ramUsedGB = (memStatus.ullTotalPhys - memStatus.ullAvailPhys) / (1024.0f * 1024.0f * 1024.0f);
        }

        ULARGE_INTEGER freeBytes, totalBytes;
        if (GetDiskFreeSpaceExA("C:\\", &freeBytes, &totalBytes, nullptr))
        {
            diskTotalGB = totalBytes.QuadPart / (1024.0f * 1024.0f * 1024.0f);
            diskUsedGB = (totalBytes.QuadPart - freeBytes.QuadPart) / (1024.0f * 1024.0f * 1024.0f);
        }
#else
        ramTotalGB = juce::SystemStats::getMemorySizeInMegabytes() / 1024.0f;
#endif

        extern BeatMate::ServiceLocator* g_serviceLocator;
        auto* engine = g_serviceLocator ? g_serviceLocator->tryGet<Core::AudioEngine>() : nullptr;
        double latencyMs = engine ? engine->getLatencyMs() : 0.0;

        content->m_statusCPU->setText("CPU: " + juce::String(cpuPercent, 1) + "%",
                                       juce::dontSendNotification);
        content->m_statusRAM->setText("RAM: " + juce::String(ramUsedGB, 1) + "/"
                                                + juce::String(ramTotalGB, 1) + " GB",
                                       juce::dontSendNotification);
        content->m_statusLatency->setText(BM_TJ("home.gauge.latency") + ": " + juce::String(latencyMs, 1) + " ms",
                                           juce::dontSendNotification);

        content->m_cpuFill = static_cast<float>(cpuPercent / 100.0);
        content->m_ramFill = ramTotalGB > 0.0f ? ramUsedGB / ramTotalGB : 0.0f;
        content->repaint(0, content->getHeight() - ContentComponent::kStatusBarHeight,
                         content->getWidth(), ContentComponent::kStatusBarHeight);

        if (g_serviceLocator)
        {
            if (auto* precache = g_serviceLocator->tryGet<Services::Library::WaveformPrecacheService>())
            {
                const int pending = precache->pendingCount();
                if (content->m_navModel && content->m_navModel->m_precachePending != pending)
                {
                    content->m_navModel->m_precachePending = pending;
                    if (content->m_navList)
                        content->m_navList->repaint();
                }
            }
        }

        // Disque dispo si label present (extension future)
        (void)diskUsedGB; (void)diskTotalGB;

        if (g_serviceLocator)
        {
            auto* audioPlayer = g_serviceLocator->tryGet<BeatMate::Core::AudioPlayer>();
            if (audioPlayer && audioPlayer->wasStoppedByLimit())
            {
                bool expired = audioPlayer->isTrialExpired();
                // Only clear the one-shot flag, NOT the permanent trialExpired
                audioPlayer->clearStoppedByLimit();

                double totalPlayed = audioPlayer->getTotalPlayedSeconds();
                double maxSec = audioPlayer->getMaxPlaybackSeconds();
                juce::String msg;
                if (expired)
                {
                    msg = BM_TJ("mainWindow.trial.expired");
                }
                else
                {
                    msg = BM_TJ("mainWindow.trial.limited");
                }
                // Alerte limite d'essai désactivée volontairement : log seulement.
                spdlog::info("[MainWindow] trial limit hit (modal disabled): {}", msg.toStdString());
            }
        }
    }
}

void MainWindow::updateNowPlaying(const juce::String& title, const juce::String& artist,
                                   double bpm, const juce::String& key)
{
    if (auto* content = dynamic_cast<ContentComponent*>(getContentComponent()))
    {
        content->m_statusTrack->setText(artist + " - " + title, juce::dontSendNotification);
        if (content->m_nowPlayingBar)
            content->m_nowPlayingBar->setTrackInfo(title, artist, bpm, key);
    }
}

void MainWindow::showFirstTimeSetup()
{
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
        BM_TJ("mainWindow.initialSetup"),
        BM_TJ("mainWindow.initialSetupMsg"),
        BM_TJ("common.ok"));
}


MainWindow::ContentComponent::~ContentComponent() {
    Widgets::ToastNotifier::setInstance(nullptr);
    extern BeatMate::ServiceLocator* g_serviceLocator;
    if (g_serviceLocator) {
        if (auto* svc = g_serviceLocator->tryGet<Services::Config::LocalizationService>())
            svc->removeChangeListener(this);
    }
}

void MainWindow::ContentComponent::changeListenerCallback(juce::ChangeBroadcaster*) {
    if (!m_firstLangCallbackConsumed) {
        m_firstLangCallbackConsumed = true;
        return;
    }

    if (m_navModel) {
        m_navModel->m_items = {
            {BM_TJ("mainWindow.nav.home"),          MainWindow::Nav_Home},
            {BM_TJ("mainWindow.nav.library"),       MainWindow::Nav_Library},
            {BM_TJ("mainWindow.nav.import"),        MainWindow::Nav_Import},
            {BM_TJ("mainWindow.nav.analysis"),      MainWindow::Nav_Analysis},
            {BM_TJ("mainWindow.nav.hotcues"),       MainWindow::Nav_HotCues},
            {BM_TJ("mainWindow.nav.normalization"), MainWindow::Nav_Normalization},
            {BM_TJ("mainWindow.nav.setPrep"),       MainWindow::Nav_SetPreparation},
            {BM_TJ("mainWindow.nav.soireePrep"),    MainWindow::Nav_SoireePreparation},
            {BM_TJ("mainWindow.nav.agenda"),        MainWindow::Nav_Agenda},
            {BM_TJ("mainWindow.nav.compare"),       MainWindow::Nav_Compare},
            {BM_TJ("mainWindow.nav.liveSuggest"),   MainWindow::Nav_BeatMateLive},
            {BM_TJ("mainWindow.nav.export"),        MainWindow::Nav_Export},
            {juce::String::fromUTF8("Mix"),        MainWindow::Nav_Mix},
            {BM_TJ("mainWindow.nav.streaming"),     MainWindow::Nav_Streaming},
            {BM_TJ("mainWindow.nav.settings"),      MainWindow::Nav_Settings},
            {BM_TJ("mainWindow.nav.help"),          MainWindow::Nav_Help}
        };
        if (m_navList) {
            m_navList->updateContent();
            m_navList->repaint();
        }
    }

    if (m_tbImport) {
        m_tbImport->setButtonText(BM_TJ("mainWindow.tb.import"));
        m_tbImport->setTooltip(BM_TJ("mainWindow.tb.import.tooltip"));
    }
    if (m_tbAnalyze) {
        m_tbAnalyze->setButtonText(BM_TJ("mainWindow.tb.analyze"));
        m_tbAnalyze->setTooltip(BM_TJ("mainWindow.tb.analyze.tooltip"));
    }
    if (m_tbHotCues) {
        m_tbHotCues->setButtonText(BM_TJ("mainWindow.tb.hotCues"));
        m_tbHotCues->setTooltip(BM_TJ("mainWindow.tb.hotCues.tooltip"));
    }
    if (m_tbNormalize) {
        m_tbNormalize->setButtonText(BM_TJ("mainWindow.tb.normalize"));
        m_tbNormalize->setTooltip(BM_TJ("mainWindow.tb.normalize.tooltip"));
    }
    if (m_tbExport) {
        m_tbExport->setButtonText(BM_TJ("mainWindow.tb.export"));
        m_tbExport->setTooltip(BM_TJ("mainWindow.tb.export.tooltip"));
    }

    if (m_statusDJSoftware) m_statusDJSoftware->setText(BM_TJ("mainWindow.status.djNotConnected"), juce::dontSendNotification);
    if (m_statusLatency)    m_statusLatency->setText(BM_TJ("mainWindow.status.latency"), juce::dontSendNotification);
    if (m_statusTrack)      m_statusTrack->setText(BM_TJ("mainWindow.status.noTrack"), juce::dontSendNotification);

    for (auto& kv : m_views)
        if (auto* r = dynamic_cast<BeatMate::UI::IRetranslatable*>(kv.second.get()))
            r->retranslateUi();
    repaint();
}

MainWindow::ContentComponent::ContentComponent(MainWindow& owner)
    : m_owner(owner)
{
    extern BeatMate::ServiceLocator* g_serviceLocator;
    if (g_serviceLocator) {
        if (auto* svc = g_serviceLocator->tryGet<Services::Config::LocalizationService>())
            svc->addChangeListener(this);
    }
    m_logoLabel = std::make_unique<juce::Label>("logo", "BeatMate");
    m_logoLabel->setFont(juce::Font("Segoe UI", 18.0f, juce::Font::bold));
    m_logoLabel->setColour(juce::Label::textColourId, Colors::primary());
    m_logoLabel->setColour(juce::Label::backgroundColourId, Colors::bgSidebar());
    m_logoLabel->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*m_logoLabel);

    m_navModel = std::make_unique<NavListModel>(*this);
    m_navModel->m_items = {
        {BM_TJ("mainWindow.nav.home"),          MainWindow::Nav_Home},
        {BM_TJ("mainWindow.nav.library"),       MainWindow::Nav_Library},
        {BM_TJ("mainWindow.nav.import"),        MainWindow::Nav_Import},
        {BM_TJ("mainWindow.nav.analysis"),      MainWindow::Nav_Analysis},
        {BM_TJ("mainWindow.nav.hotcues"),       MainWindow::Nav_HotCues},
        {BM_TJ("mainWindow.nav.normalization"), MainWindow::Nav_Normalization},
        {BM_TJ("mainWindow.nav.setPrep"),       MainWindow::Nav_SetPreparation},
        {BM_TJ("mainWindow.nav.soireePrep"),    MainWindow::Nav_SoireePreparation},
        {BM_TJ("mainWindow.nav.agenda"),        MainWindow::Nav_Agenda},
        {BM_TJ("mainWindow.nav.compare"),       MainWindow::Nav_Compare},
        {BM_TJ("mainWindow.nav.liveSuggest"),   MainWindow::Nav_BeatMateLive},
        {BM_TJ("mainWindow.nav.export"),        MainWindow::Nav_Export},
            {juce::String::fromUTF8("Mix"),        MainWindow::Nav_Mix},
        {BM_TJ("mainWindow.nav.streaming"),     MainWindow::Nav_Streaming},
        {BM_TJ("mainWindow.nav.settings"),      MainWindow::Nav_Settings},
        {BM_TJ("mainWindow.nav.help"),          MainWindow::Nav_Help}
    };

    m_navList = std::make_unique<HoverableListBox>("navList", m_navModel.get());
    m_navList->setRowHeight(44);
    m_navList->setColour(juce::ListBox::backgroundColourId, Colors::bgSidebar());
    m_navList->setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    m_navList->onRowHovered = [this](int row) {
        m_navModel->m_hoveredRow = row;
        m_navList->repaint();
    };
    m_navList->onMouseExited = [this]() {
        m_navModel->m_hoveredRow = -1;
        m_navList->repaint();
    };
    addAndMakeVisible(*m_navList);

    setMouseClickGrabsKeyboardFocus(false);

    auto makeToolBtn = [this](const juce::String& text, int navTarget, const juce::String& tooltip) {
        auto btn = std::make_unique<juce::TextButton>(text);
        btn->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        btn->setColour(juce::TextButton::buttonOnColourId, Colors::primary().withAlpha(0.25f));
        btn->setColour(juce::TextButton::textColourOffId, Colors::textSecondary());
        btn->setColour(juce::TextButton::textColourOnId, Colors::textPrimary());
        btn->setTooltip(tooltip);
        btn->onClick = [this, navTarget] { navigateTo(navTarget); };
        addAndMakeVisible(*btn);
        return btn;
    };

    m_tbImport    = makeToolBtn(BM_TJ("mainWindow.tb.import"),    MainWindow::Nav_Import,        BM_TJ("mainWindow.tb.import.tooltip"));
    m_tbAnalyze   = makeToolBtn(BM_TJ("mainWindow.tb.analyze"),   MainWindow::Nav_Analysis,      BM_TJ("mainWindow.tb.analyze.tooltip"));
    m_tbHotCues   = makeToolBtn(BM_TJ("mainWindow.tb.hotCues"),   MainWindow::Nav_HotCues,       BM_TJ("mainWindow.tb.hotCues.tooltip"));
    m_tbNormalize = makeToolBtn(BM_TJ("mainWindow.tb.normalize"), MainWindow::Nav_Normalization, BM_TJ("mainWindow.tb.normalize.tooltip"));
    m_tbExport    = makeToolBtn(BM_TJ("mainWindow.tb.export"),    MainWindow::Nav_Export,        BM_TJ("mainWindow.tb.export.tooltip"));
    m_tbAgenda    = makeToolBtn(juce::String::fromUTF8("Agenda"), MainWindow::Nav_Agenda,
                                juce::String::fromUTF8("Vos dates de prestation (calendrier, exports)"));

    m_toolbarTargets = {
        { m_tbImport.get(),    MainWindow::Nav_Import },
        { m_tbAnalyze.get(),   MainWindow::Nav_Analysis },
        { m_tbHotCues.get(),   MainWindow::Nav_HotCues },
        { m_tbNormalize.get(), MainWindow::Nav_Normalization },
        { m_tbExport.get(),    MainWindow::Nav_Export },
        { m_tbAgenda.get(),    MainWindow::Nav_Agenda }
    };

    m_agendaReminderBtn = std::make_unique<juce::TextButton>();
    m_agendaReminderBtn->setColour(juce::TextButton::buttonColourId, Colors::warning().withAlpha(0.22f));
    m_agendaReminderBtn->setColour(juce::TextButton::textColourOffId, Colors::warning());
    m_agendaReminderBtn->setTooltip(juce::String::fromUTF8("Prestation imminente \xe2\x80\x94 cliquez pour ouvrir l'Agenda"));
    m_agendaReminderBtn->onClick = [this] {
        acknowledgeReminders();
        navigateTo(MainWindow::Nav_Agenda);
    };
    m_agendaReminderBtn->setVisible(false);
    addAndMakeVisible(*m_agendaReminderBtn);

    m_statusDJSoftware = std::make_unique<juce::Label>("djSw", BM_TJ("mainWindow.status.djNotConnected"));
    m_statusDJSoftware->setFont(juce::Font("Segoe UI", 11.0f, juce::Font::plain));
    m_statusDJSoftware->setColour(juce::Label::textColourId, Colors::error());
    addAndMakeVisible(*m_statusDJSoftware);

    m_statusCPU = std::make_unique<juce::Label>("cpu", "CPU: 0%");
    m_statusCPU->setFont(juce::Font("Consolas", 11.0f, juce::Font::plain));
    m_statusCPU->setColour(juce::Label::textColourId, Colors::textMuted());
    addAndMakeVisible(*m_statusCPU);

    m_statusRAM = std::make_unique<juce::Label>("ram", "RAM: 0 MB");
    m_statusRAM->setFont(juce::Font("Consolas", 11.0f, juce::Font::plain));
    m_statusRAM->setColour(juce::Label::textColourId, Colors::textMuted());
    addAndMakeVisible(*m_statusRAM);

    m_statusLatency = std::make_unique<juce::Label>("lat", BM_TJ("mainWindow.status.latency"));
    m_statusLatency->setFont(juce::Font("Consolas", 11.0f, juce::Font::plain));
    m_statusLatency->setColour(juce::Label::textColourId, Colors::textMuted());
    addAndMakeVisible(*m_statusLatency);

    m_statusTrack = std::make_unique<juce::Label>("track", BM_TJ("mainWindow.status.noTrack"));
    m_statusTrack->setFont(juce::Font("Segoe UI", 11.0f, juce::Font::plain));
    m_statusTrack->setColour(juce::Label::textColourId, Colors::textSecondary());
    m_statusTrack->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*m_statusTrack);

    m_statusCreator = std::make_unique<juce::Label>("creator", "BeatMate V12 by Sebastien Sainte-Foi");
    m_statusCreator->setFont(juce::Font("Segoe UI", 10.0f, juce::Font::plain));
    m_statusCreator->setColour(juce::Label::textColourId, Colors::textMuted());
    m_statusCreator->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*m_statusCreator);

    m_nowPlayingBar = std::make_unique<NowPlayingBar>();
    NowPlayingBar::setInstance(m_nowPlayingBar.get());

    m_favoritesBar = std::make_unique<Widgets::FavoritesBar>();
    addAndMakeVisible(*m_favoritesBar);
    addAndMakeVisible(*m_nowPlayingBar);

    createViews();

    navigateTo(MainWindow::Nav_Home);

    setWantsKeyboardFocus(true);

    // Toast notifier overlay — created LAST so it sits on top of the Z-order.
    m_toastNotifier = std::make_unique<Widgets::ToastNotifier>();
    addAndMakeVisible(m_toastNotifier.get());
    Widgets::ToastNotifier::setInstance(m_toastNotifier.get());

    m_tooltipWindow = std::make_unique<juce::TooltipWindow>(this, 650);

    setSize(1400, 900);
}

struct MainWindow::ContentComponent::ListenerStorage
{
    struct HomeListener : HomeView::Listener
    {
        MainWindow::ContentComponent& owner;
        explicit HomeListener(ContentComponent& o) : owner(o) {}

        void navigateToLibrary() override
        {
            spdlog::info("[HomeView] Navigate to Library");
            owner.navigateTo(MainWindow::Nav_Library);
        }
        void navigateToAnalysis() override
        {
            spdlog::info("[HomeView] Navigate to Analysis");
            owner.navigateTo(MainWindow::Nav_Analysis);
        }
        void navigateToImport() override
        {
            spdlog::info("[HomeView] Navigate to Import");
            owner.navigateTo(MainWindow::Nav_Import);
        }
    };
    std::unique_ptr<HomeListener> homeListener;

    struct ImportListener : ImportView::Listener
    {
        Services::Library::TrackDataProvider* provider;
        ImportView* view;
        std::shared_ptr<std::atomic<bool>> aliveFlag = std::make_shared<std::atomic<bool>>(true);
        std::atomic<bool> importRunning{false};
        std::thread importThread_;
        std::vector<int64_t> lastImportedIds;
        std::function<void(std::vector<Models::Track>)> onAutoAnalyze;
        explicit ImportListener(Services::Library::TrackDataProvider* p, ImportView* v)
            : provider(p), view(v) {}
        ~ImportListener()
        {
            aliveFlag->store(false);
            if (importThread_.joinable()) importThread_.join();
        }

        void importRequested(const std::vector<Services::Library::StagedImportEntry>& entries,
                             const Services::Library::FileImportOptions& options) override
        {
            if (!provider) { spdlog::warn("[Import] No TrackDataProvider"); return; }
            if (importRunning.exchange(true)) {
                spdlog::warn("[Import] Import deja en cours, ignore");
                return;
            }

            spdlog::info("[Import] Importing {} staged file(s) (readTags={}, dedup={}, copy={})",
                         entries.size(), options.readTags, options.detectDuplicates,
                         options.copyToLibrary);

            auto providerCopy = provider;
            auto viewCopy = view;
            auto alive = aliveFlag;
            auto* running = &importRunning;
            auto* self = this;

            if (importThread_.joinable()) importThread_.join();
            importThread_ = std::thread([self, providerCopy, viewCopy, alive, running,
                                         entries, options]()
            {
                extern BeatMate::ServiceLocator* g_serviceLocator;
                auto* importer = g_serviceLocator
                    ? g_serviceLocator->tryGet<Services::Library::TrackImporter>()
                    : nullptr;

                Services::Library::FileImportReport report;
                if (importer)
                {
                    providerCopy->beginBatch();
                    report = importer->importFiles(entries, options,
                        [viewCopy, alive](const Services::Library::ImportProgress& p) {
                            const int cur = p.processed;
                            const int tot = p.total;
                            const juce::String name =
                                juce::File(juce::String::fromUTF8(p.currentFile.c_str())).getFileName();
                            juce::MessageManager::callAsync([cur, tot, name, viewCopy, alive]() {
                                if (!alive->load()) return;
                                if (viewCopy) viewCopy->onImportProgress(cur, tot, name);
                            });
                        });
                    providerCopy->endBatch();
                }
                else
                {
                    spdlog::error("[Import] TrackImporter service unavailable");
                    report.errors = static_cast<int>(entries.size());
                }

                if (alive->load())
                    running->store(false);

                spdlog::info("[Import] Done: {} imported, {} duplicates, {} errors",
                             report.imported, report.duplicates, report.errors);

                const bool autoAnalyze = options.autoAnalyze;
                juce::MessageManager::callAsync([self, viewCopy, alive, report, autoAnalyze]() {
                    if (!alive->load()) return;
                    self->lastImportedIds = report.importedIds;
                    if (viewCopy) viewCopy->onImportFinished(report);
                    const juce::String title = juce::String(report.imported) + " "
                                               + BM_TJ("import.summary.imported");
                    juce::String detail;
                    if (report.duplicates > 0)
                        detail = juce::String(report.duplicates) + " " + BM_TJ("import.summary.duplicates");
                    if (report.errors > 0)
                        detail += (detail.isEmpty() ? juce::String() : juce::String(", "))
                                  + juce::String(report.errors) + " " + BM_TJ("import.summary.errors");
                    auto kind = BeatMate::UI::Widgets::ToastNotifier::Kind::Success;
                    if (report.imported == 0)
                        kind = report.errors > 0 ? BeatMate::UI::Widgets::ToastNotifier::Kind::Error
                                                 : BeatMate::UI::Widgets::ToastNotifier::Kind::Warning;
                    BeatMate::UI::Widgets::ToastNotifier::getInstance().show(title, detail, kind, 6000);
                    if (autoAnalyze && report.imported > 0)
                        self->triggerAnalysis();
                });
            });
        }

        void importCancelRequested() override
        {
            extern BeatMate::ServiceLocator* g_serviceLocator;
            if (!g_serviceLocator) return;
            if (auto* importer = g_serviceLocator->tryGet<Services::Library::TrackImporter>())
                importer->cancel();
        }

        void analyzeImportedRequested() override
        {
            triggerAnalysis();
        }

        void triggerAnalysis()
        {
            if (!provider || !onAutoAnalyze || lastImportedIds.empty())
                return;
            std::vector<Models::Track> tracks;
            tracks.reserve(lastImportedIds.size());
            for (auto id : lastImportedIds)
            {
                auto t = provider->getTrack(id);
                if (t.id == id)
                    tracks.push_back(std::move(t));
            }
            if (!tracks.empty())
            {
                spdlog::info("[Import] Auto-analysis on {} imported tracks", tracks.size());
                onAutoAnalyze(std::move(tracks));
            }
        }
    };
    std::unique_ptr<ImportListener> importListener;

    struct AnalysisListener : AnalysisView::Listener
    {
        Services::Library::TrackDataProvider* provider;
        AnalysisView* view;
        std::unique_ptr<Services::Analysis::AnalysisRunner> runner;
        std::function<void(int, int)> onBadgeProgress;

        explicit AnalysisListener(Services::Library::TrackDataProvider* p, AnalysisView* v)
            : provider(p), view(v)
        {
            if (provider)
                runner = std::make_unique<Services::Analysis::AnalysisRunner>(*provider);
        }

        void runAnalysis(std::vector<Models::Track> tracks)
        {
            if (!runner || !view)
                return;

            Services::Analysis::AnalysisOptions options;
            options.bpm       = view->isBPMEnabled();
            options.key       = view->isKeyEnabled();
            options.energy    = view->isEnergyEnabled();
            options.structure = view->isStructureEnabled();
            options.waveform  = view->isWaveformEnabled();
            options.mood      = view->isMoodEnabled();

            bool optUltraStems = false;
            bool stemsLicensed = true;
            {
                extern BeatMate::ServiceLocator* g_serviceLocator;
                if (g_serviceLocator) {
                    if (auto* settings = g_serviceLocator->tryGet<Services::Config::SettingsManager>())
                        optUltraStems = settings->get<bool>("stems_ultra_mdx_gpu", false);
                    if (auto* lic = g_serviceLocator->tryGet<Services::Security::LicenseService>())
                        stemsLicensed = lic->canUseStems();
                }
            }
            if (optUltraStems && ! stemsLicensed) {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                    juce::String::fromUTF8("S\xc3\xa9paration en stems"),
                    juce::String::fromUTF8(
                        "La s\xc3\xa9paration en stems fait partie des versions payantes de "
                        "BeatMate. L'analyse continue sans elle.\n\nActivez votre licence dans "
                        "Param\xc3\xa8tres > Licence pour l'utiliser."),
                    BM_TJ("common.ok"));
            }
            options.ultraStems = optUltraStems && stemsLicensed
                               && Core::Stems::StemSepSotaService::isAvailable();

            juce::Component::SafePointer<AnalysisView> safeView(view);
            Services::Analysis::AnalysisCallbacks callbacks;
            callbacks.onTrackStarted = [safeView](const juce::String& path) {
                if (safeView) safeView->onTrackStarted(path);
            };
            callbacks.onTrackFinished = [safeView](const Services::Analysis::TrackRowResult& r) {
                if (safeView) safeView->onTrackFinished(r);
            };
            auto badgeCb = onBadgeProgress;
            callbacks.onProgress = [safeView, badgeCb](int processed, int total, int skipped) {
                if (safeView) safeView->onBatchProgress(processed, total, skipped);
                if (badgeCb) badgeCb(processed, total);
            };
            callbacks.onFinished = [safeView, badgeCb](int processed, int total, int skipped, bool cancelled) {
                if (safeView) safeView->onBatchFinished(processed, total, skipped, cancelled);
                if (badgeCb) badgeCb(0, 0);
            };

            if (!runner->start(std::move(tracks), options, std::move(callbacks))) {
                spdlog::warn("[Analysis] Runner refused to start (already running or no tracks)");
                view->onBatchFinished(0, 0, 0, true);
            }
        }

        void analyzeAllRequested() override
        {
            if (!provider) { spdlog::warn("[Analysis] No TrackDataProvider"); return; }
            auto tracks = provider->getAllTracks();
            spdlog::info("[Analysis] Analyze ALL requested ({} tracks)", tracks.size());
            runAnalysis(std::move(tracks));
        }

        void analyzeSelectedRequested(const juce::StringArray& selectedPaths) override
        {
            if (!provider) { spdlog::warn("[Analysis] No TrackDataProvider"); return; }
            spdlog::info("[Analysis] Analyze selected requested ({} paths)", selectedPaths.size());

            std::unordered_map<std::string, Models::Track> byPath;
            for (auto& t : provider->getAllTracks())
                byPath.emplace(t.filePath, t);

            std::vector<Models::Track> selected;
            for (auto& path : selectedPaths) {
                auto it = byPath.find(path.toStdString());
                if (it != byPath.end())
                    selected.push_back(it->second);
                else
                    spdlog::warn("[Analysis] Selected path not found in library: {}", path.toStdString());
            }

            if (selected.empty()) {
                spdlog::warn("[Analysis] No selected track resolved to a library entry, aborting");
                if (view)
                    view->onBatchFinished(0, 0, 0, true);
                return;
            }

            runAnalysis(std::move(selected));
        }

        void analysisCancelled() override
        {
            spdlog::info("[Analysis] Cancel requested by user");
            if (runner)
                runner->cancel();
        }
    };
    std::unique_ptr<AnalysisListener> analysisListener;

    struct ExportListener : ExportView::Listener
    {
        Services::Library::TrackDataProvider* provider;
        ExportView* view;
        explicit ExportListener(Services::Library::TrackDataProvider* p, ExportView* v)
            : provider(p), view(v) {}

        void exportRequested() override
        {
            spdlog::info("[Export] Export requested (handled by BatchExportService)");
        }

        void exportCompleted(int count) override
        {
            spdlog::info("[Export] Export completed: {} files", count);
        }
    };
    std::unique_ptr<ExportListener> exportListener;

    struct HotCueListener : HotCueView::Listener
    {
        Services::Library::TrackDataProvider* provider;
        HotCueView* view = nullptr;
        std::atomic<bool> seratoExportBusy{false};
        explicit HotCueListener(Services::Library::TrackDataProvider* p) : provider(p) {}

        void cuePointSet(int number, double position) override
        {
            spdlog::info("[HotCue] Cue {} set at {:.3f}s", number, position);
        }

        void cuePointRemoved(int number) override
        {
            spdlog::info("[HotCue] Cue {} removed", number);
        }

        void autoGenerateRequested() override
        {
            spdlog::info("[HotCue] Auto-generate cue points requested");
            // CuePointGenerator exige un AudioTrack chargé : géré ailleurs.
        }

        void exportCuesRequested(const juce::String& target) override
        {
            spdlog::info("[HotCue] Export cues to: {}", target.toStdString());

            {
                extern BeatMate::ServiceLocator* g_serviceLocator;
                if (g_serviceLocator) {
                    if (auto* lic = g_serviceLocator->tryGet<Services::Security::LicenseService>()) {
                        if (! lic->canUseExport()) {
                            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                                juce::String::fromUTF8("Export des hot cues"),
                                juce::String::fromUTF8(
                                    "L'export vers les logiciels DJ fait partie des versions "
                                    "payantes de BeatMate.\n\nActivez votre licence dans "
                                    "Param\xc3\xa8tres > Licence."),
                                BM_TJ("common.ok"));
                            return;
                        }
                    }
                }
            }

            if (!provider || !view) return;
            int64_t trackId = view->getCurrentTrackId();
            if (trackId <= 0) {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                    BM_TJ("mainWindow.export.title"), BM_TJ("mainWindow.export.noTrack"));
                return;
            }

            auto track = provider->getTrack(trackId);
            auto cues = provider->getCuePoints(trackId);

            // Dialogue asynchrone : le browseForFileToSave modal bloquait l'UI.
            auto doExport = [](const std::string& ext, const std::string& label,
                               std::function<bool(const std::string&)> exportFn) {
                auto chooser = std::make_shared<juce::FileChooser>(
                    BM_TJ("mainWindow.export.exportTo") + " " + juce::String(label),
                    juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
                    "*." + juce::String(ext));
                const juce::String labelStr(label);
                chooser->launchAsync(
                    juce::FileBrowserComponent::saveMode
                    | juce::FileBrowserComponent::canSelectFiles
                    | juce::FileBrowserComponent::warnAboutOverwriting,
                    [chooser, exportFn, labelStr, ext](const juce::FileChooser& fc) {
                        auto file = fc.getResult();
                        if (file == juce::File{}) return;
                        const std::string path =
                            file.withFileExtension(juce::String(ext)).getFullPathName().toStdString();
                        auto* pool = BeatMate::getBackgroundPool();
                        auto runWrite = [exportFn, path, labelStr]() {
                            const bool ok = exportFn(path);
                            juce::MessageManager::callAsync([ok, path, labelStr]() {
                                if (ok) {
                                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                                        BM_TJ("mainWindow.export.title") + juce::String(" ") + labelStr,
                                        BM_TJ("mainWindow.export.success") + juce::String("\n") + juce::String(path));
                                } else {
                                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                        BM_TJ("mainWindow.export.title"), BM_TJ("mainWindow.export.error"));
                                }
                            });
                        };
                        if (pool) pool->addJob(runWrite); else runWrite();
                    });
            };

            if (target == "Rekordbox") {
                auto exp = std::make_shared<Services::Rekordbox::RekordboxXmlExporter>();
                exp->addTrack(Services::Rekordbox::RekordboxXmlExporter::fromBeatMateTrack(track, cues));
                doExport("xml", "Rekordbox XML", [exp](const std::string& p){ return exp->exportToFile(p); });
            }
            else if (target == "Traktor") {
                auto exp = std::make_shared<Services::Traktor::TraktorNmlExporter>();
                exp->addTrack(Services::Traktor::TraktorNmlExporter::fromBeatMateTrack(track, cues));
                doExport("nml", "Traktor NML", [exp](const std::string& p){ return exp->exportToFile(p); });
            }
            else if (target == "VirtualDJ") {
                auto exp = std::make_shared<Services::VirtualDJ::VirtualDJExporter>();
                exp->addTrack(Services::VirtualDJ::VirtualDJExporter::fromBeatMateTrack(track, cues));
                doExport("xml", "VirtualDJ XML", [exp](const std::string& p){ return exp->exportToFile(p); });
            }
            else if (target == "Serato") {
                // Serato écrit des tags ID3 GEOB directement dans le fichier.
                if (seratoExportBusy.exchange(true)) {
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                        BM_TJ("mainWindow.export.serato.title"),
                        juce::String::fromUTF8("Un export Serato est deja en cours."));
                    return;
                }
                auto seratoCues = Services::Serato::SeratoTagWriter::fromBeatMateCues(cues);
                const std::string filePath = track.filePath;
                const float bpm = static_cast<float>(track.bpm);
                auto* busy = &seratoExportBusy;
                auto runWrite = [filePath, seratoCues, bpm, busy]() {
                    Services::Serato::SeratoTagWriter writer;
                    const bool ok = writer.writeToFile(filePath, seratoCues, {}, bpm);
                    busy->store(false);
                    juce::MessageManager::callAsync([ok]() {
                        if (ok) {
                            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                                BM_TJ("mainWindow.export.serato.title"), BM_TJ("mainWindow.export.serato.success"));
                        } else {
                            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                BM_TJ("mainWindow.export.serato.title"), BM_TJ("mainWindow.export.serato.error"));
                        }
                    });
                };
                if (auto* pool = BeatMate::getBackgroundPool()) pool->addJob(runWrite);
                else { runWrite(); }
            }
            else if (target == "Engine DJ") {
                // Engine DJ uses SQLite - export as Rekordbox XML (Engine DJ can import it)
                auto exp = std::make_shared<Services::Rekordbox::RekordboxXmlExporter>();
                exp->addTrack(Services::Rekordbox::RekordboxXmlExporter::fromBeatMateTrack(track, cues));
                doExport("xml", "Engine DJ (Rekordbox XML)", [exp](const std::string& p){ return exp->exportToFile(p); });
            }
        }
    };
    std::unique_ptr<HotCueListener> hotCueListener;

    struct NormalizationListener : NormalizationView::Listener
    {
        Services::Library::TrackDataProvider* provider;
        NormalizationView* view;
        std::shared_ptr<std::atomic<bool>> aliveFlag = std::make_shared<std::atomic<bool>>(true);
        std::shared_ptr<std::atomic<bool>> cancelFlag = std::make_shared<std::atomic<bool>>(false);
        explicit NormalizationListener(Services::Library::TrackDataProvider* p, NormalizationView* v)
            : provider(p), view(v) {}
        ~NormalizationListener() { aliveFlag->store(false); cancelFlag->store(true); }

        void runNormalization(std::vector<Models::Track> tracks)
        {
            if (tracks.empty()) {
                spdlog::warn("[Normalization] No tracks to normalize");
                return;
            }

            auto providerCopy = provider;
            auto viewPtr = view;
            auto alive = aliveFlag;
            auto cancel = cancelFlag;
            float targetLUFS = viewPtr ? viewPtr->getTargetLUFS() : -14.0f;

            cancel->store(false);

            std::thread([providerCopy, viewPtr, alive, cancel, targetLUFS,
                         tracks = std::move(tracks)]()
            {
                Services::Normalization::LoudnessNormalizer normalizer;
                providerCopy->beginBatch();

                int processed = 0;
                int total = static_cast<int>(tracks.size());

                for (size_t i = 0; i < tracks.size(); ++i)
                {
                    if (cancel->load()) {
                        spdlog::info("[Normalization] Cancelled at {}/{}", processed, total);
                        break;
                    }

                    auto track = tracks[i];
                    spdlog::info("[Normalization] Processing {}/{}: {}",
                        i + 1, tracks.size(), track.title);

                    int iCopy = static_cast<int>(i);
                    int totalCopy = static_cast<int>(tracks.size());
                    std::string titleCopy = track.title;
                    juce::MessageManager::callAsync([viewPtr, alive, iCopy, totalCopy, titleCopy]() {
                        if (!alive->load()) return;
                        if (viewPtr) viewPtr->updateProgress(iCopy, totalCopy);
                    });

                    // ITU-R BS.1770-4 / EBU R128 measurement
                    float measuredLUFS = -23.0f;
                    float truePeakDBTP = 0.0f;
                    float gainAdjustDB = 0.0f;

                    Core::AudioFileReader audioReader;
                    auto audioTrack = audioReader.readFile(track.filePath);
                    if (audioTrack && !cancel->load())
                    {
                        measuredLUFS = normalizer.measureLUFS(*audioTrack);

                        const float* rawData = audioTrack->getRawData();
                        size_t totalSamples = audioTrack->getTotalSamples();
                        float peakSample = 0.0f;
                        for (size_t s = 0; s < totalSamples; ++s) {
                            float absVal = std::abs(rawData[s]);
                            if (absVal > peakSample) peakSample = absVal;
                        }
                        truePeakDBTP = (peakSample > 0.0f)
                            ? 20.0f * std::log10(peakSample) : -100.0f;
                    }

                    if (cancel->load()) break;

                    // Gain to target LUFS, clamped by True Peak ceiling (-1 dBTP)
                    gainAdjustDB = targetLUFS - measuredLUFS;
                    float maxSafeGain = -1.0f - truePeakDBTP;
                    if (gainAdjustDB > maxSafeGain)
                        gainAdjustDB = maxSafeGain;
                    gainAdjustDB = std::max(-20.0f, std::min(gainAdjustDB, 20.0f));

                    float finalLUFS = measuredLUFS + gainAdjustDB;
                    spdlog::info("[Normalization] '{}': {:.1f} LUFS, {:.1f} dBTP, gain={:.1f} dB -> {:.1f} LUFS",
                        track.title, measuredLUFS, truePeakDBTP, gainAdjustDB, finalLUFS);

                    juce::String titleStr = juce::String(track.title);
                    juce::MessageManager::callAsync(
                        [viewPtr, alive, titleStr, measuredLUFS, finalLUFS]() {
                            if (!alive->load()) return;
                            if (viewPtr) viewPtr->setCurrentMeasurement(titleStr, measuredLUFS, finalLUFS);
                        });

                    // ReplayGain-style non-destructive metadata
                    track.comment = "LUFS:" + std::to_string(measuredLUFS)
                                  + " Peak:" + std::to_string(truePeakDBTP) + "dBTP"
                                  + " Gain:" + std::to_string(gainAdjustDB) + "dB"
                                  + " Target:" + std::to_string(targetLUFS) + "LUFS";
                    providerCopy->updateTrack(track);

                    processed++;
                    int p = processed; int t = total;
                    juce::MessageManager::callAsync([viewPtr, alive, p, t]() {
                        if (!alive->load()) return;
                        if (viewPtr) viewPtr->updateProgress(p, t);
                    });
                }

                providerCopy->endBatch();
                spdlog::info("[Normalization] Done: {}/{} tracks processed", processed, total);
            }).detach();
        }

        void normalizeRequested() override
        {
            spdlog::info("[Normalization] Normalize ALL requested");
            if (!provider || !view) return;

            auto titles = view->getTrackTitles();
            std::vector<Models::Track> tracks;

            if (titles.empty()) {
                tracks = provider->getAllTracks();
            } else {
                auto allTracks = provider->getAllTracks();
                for (auto& t : allTracks) {
                    for (auto& title : titles) {
                        if (title.toStdString() == t.title) {
                            tracks.push_back(t);
                            break;
                        }
                    }
                }
            }

            spdlog::info("[Normalization] Will normalize {} tracks", tracks.size());
            runNormalization(std::move(tracks));
        }

        void cancelRequested() override
        {
            spdlog::info("[Normalization] Cancel requested");
            cancelFlag->store(true);
        }

        void previewRequested(int trackIndex) override
        {
            spdlog::info("[Normalization] Preview requested for track index {}", trackIndex);
            if (!provider || !view) return;

            juce::String rowTitle = view->getTitleAtRow(trackIndex);
            if (rowTitle.isEmpty()) return;

            Models::Track found;
            bool ok = false;
            for (auto& t : provider->getAllTracks()) {
                if (juce::String(t.title) == rowTitle) { found = t; ok = true; break; }
            }
            if (!ok) { spdlog::warn("[Normalization] Preview: no DB row matches '{}'", rowTitle.toStdString()); return; }

            auto providerCopy = provider;
            auto viewPtr      = view;
            auto alive        = aliveFlag;
            float targetLUFS  = view->getTargetLUFS();

            // Off-thread LUFS measurement (ITU-R BS.1770-4).
            std::thread([providerCopy, viewPtr, alive, targetLUFS, found]() {
                Services::Normalization::LoudnessNormalizer normalizer;
                Core::AudioFileReader reader;
                auto audioTrack = reader.readFile(found.filePath);
                if (!audioTrack) return;
                float measuredLUFS = normalizer.measureLUFS(*audioTrack);

                const float* raw = audioTrack->getRawData();
                size_t n = audioTrack->getTotalSamples();
                float peak = 0.0f;
                for (size_t s = 0; s < n; ++s) { float a = std::abs(raw[s]); if (a > peak) peak = a; }
                float truePeakDB = (peak > 0.0f) ? 20.0f * std::log10(peak) : -100.0f;

                float gain = targetLUFS - measuredLUFS;
                float safeMax = -1.0f - truePeakDB;
                if (gain > safeMax) gain = safeMax;
                gain = std::max(-20.0f, std::min(gain, 20.0f));
                float finalLUFS = measuredLUFS + gain;

                juce::String titleStr = juce::String(found.title);
                juce::MessageManager::callAsync(
                    [viewPtr, alive, titleStr, measuredLUFS, finalLUFS]() {
                        if (!alive->load()) return;
                        if (viewPtr) viewPtr->setCurrentMeasurement(titleStr, measuredLUFS, finalLUFS);
                    });
                spdlog::info("[Normalization] Preview '{}': measured={:.1f} LUFS peak={:.1f} dBTP target={:.1f} -> final={:.1f}",
                    found.title, measuredLUFS, truePeakDB, targetLUFS, finalLUFS);
            }).detach();
        }

        void normalizationComplete() override
        {
            spdlog::info("[Normalization] Normalization complete");
        }
    };
    std::unique_ptr<NormalizationListener> normalizationListener;

    struct SetPrepListener : SetPreparationView::Listener
    {
        void setlistExportRequested() override
        {
            spdlog::info("[SetPrep] Setlist export requested");

            juce::FileChooser chooser(BM_TJ("mainWindow.export.setlist"), juce::File{}, "*.txt;*.pdf");
            if (chooser.browseForFileToSave(true))
            {
                auto file = chooser.getResult();
                spdlog::info("[SetPrep] Exporting setlist to: {}", file.getFullPathName().toStdString());
            }
        }
    };
    std::unique_ptr<SetPrepListener> setPrepListener;

    struct PlaylistPrepListener : PlaylistPreparationView::Listener
    {
        Services::Library::TrackDataProvider* provider;
        explicit PlaylistPrepListener(Services::Library::TrackDataProvider* p) : provider(p) {}

        void playlistSaved() override
        {
            spdlog::info("[PlaylistPrep] Playlist saved");
        }

        void playlistExportRequested(const juce::String& format) override
        {
            spdlog::info("[PlaylistPrep] Export requested: {}", format.toStdString());

            juce::String ext = format.toLowerCase() == "m3u" ? "*.m3u" :
                               format.toLowerCase() == "pls" ? "*.pls" :
                               format.toLowerCase() == "json" ? "*.json" :
                               format.toLowerCase() == "pdf" ? "*.pdf" : "*.m3u";

            juce::FileChooser chooser(BM_TJ("mainWindow.export.playlist"), juce::File{}, ext);
            if (chooser.browseForFileToSave(true))
            {
                auto file = chooser.getResult();
                spdlog::info("[PlaylistPrep] Exporting to: {}", file.getFullPathName().toStdString());

                if (provider && (format.toLowerCase() == "m3u" || format.toLowerCase() == "pls"))
                {
                    auto tracks = provider->getAllTracks();
                    Services::Export::PlaylistExportService exportSvc;
                    if (format.toLowerCase() == "m3u")
                        exportSvc.exportM3U(tracks, file.getFullPathName().toStdString());
                    else
                        exportSvc.exportPLS(tracks, file.getFullPathName().toStdString());
                }
            }
        }
    };
    std::unique_ptr<PlaylistPrepListener> playlistPrepListener;

    struct SoireePrepListener : SoireePreparationView::Listener
    {
        Services::Library::TrackDataProvider* provider;
        explicit SoireePrepListener(Services::Library::TrackDataProvider* p) : provider(p) {}

        void soireeExportRequested(const juce::String& format) override
        {
            spdlog::info("[SoireePrep] Export requested: {}", format.toStdString());

            juce::String ext = format.toLowerCase() == "pdf" ? "*.pdf" : "*.json";
            juce::FileChooser chooser(BM_TJ("mainWindow.export.soiree"), juce::File{}, ext);
            if (chooser.browseForFileToSave(true))
            {
                auto file = chooser.getResult();
                spdlog::info("[SoireePrep] Exporting to: {}", file.getFullPathName().toStdString());

                if (format.toLowerCase() == "json" && provider)
                {
                    auto tracks = provider->getAllTracks();
                    nlohmann::json j;
                    j["trackCount"] = tracks.size();
                    nlohmann::json arr = nlohmann::json::array();
                    for (auto& t : tracks)
                    {
                        nlohmann::json tj;
                        tj["title"] = t.title;
                        tj["artist"] = t.artist;
                        tj["bpm"] = t.bpm;
                        tj["key"] = t.key;
                        arr.push_back(tj);
                    }
                    j["tracks"] = arr;
                    file.replaceWithText(juce::String(j.dump(2)));
                }
            }
        }

        void soireeSaved() override
        {
            spdlog::info("[SoireePrep] Soiree saved");
        }
    };
    std::unique_ptr<SoireePrepListener> soireePrepListener;
};

void MainWindow::ContentComponent::createViews()
{
    m_viewport = std::make_unique<juce::Viewport>();
    m_viewport->setScrollBarsShown(true, false);
    m_viewport->setScrollBarThickness(8);
    addAndMakeVisible(*m_viewport);

    extern BeatMate::ServiceLocator* g_serviceLocator;
    Services::Library::TrackDataProvider* provider = nullptr;
    Core::AudioPlayer* audioPlayer = nullptr;
    Core::AudioFileReader* audioFileReader = nullptr;
    if (g_serviceLocator)
    {
        provider = g_serviceLocator->tryGet<Services::Library::TrackDataProvider>();
        audioPlayer = g_serviceLocator->tryGet<Core::AudioPlayer>();
        audioFileReader = g_serviceLocator->tryGet<Core::AudioFileReader>();
    }

    Core::StreamingPlayer* streamingPlayer = nullptr;
    if (g_serviceLocator) {
        streamingPlayer = g_serviceLocator->tryGet<Core::StreamingPlayer>();
    }
    if (m_nowPlayingBar)
    {
        m_nowPlayingBar->setAudioPlayer(audioPlayer);
        m_nowPlayingBar->setAudioFileReader(audioFileReader);
        m_nowPlayingBar->setStreamingPlayer(streamingPlayer);
    }

    auto homeView = std::make_unique<HomeView>(provider);
    homeView->onNavigateToModule = [this](int idx) { navigateTo(idx); };
    m_views[MainWindow::Nav_Home] = std::move(homeView);
    auto libraryView = std::make_unique<LibraryView>(provider);
    libraryView->onTrackPreview = [this, provider](const juce::String& title, const juce::String& artist,
                                          double bpm, const juce::String& key,
                                          const juce::String& filePath) {
        spdlog::info("onTrackPreview: title={}, filePath='{}'", title.toStdString(), filePath.toStdString());

        m_owner.updateNowPlaying(title, artist, bpm, key);

        if (m_nowPlayingBar && filePath.isNotEmpty())
        {
            spdlog::info("Calling loadAndPlay: {}", filePath.toStdString());
            m_nowPlayingBar->loadAndPlay(filePath, title, artist, bpm, key);
            if (provider)
                provider->recordPlayByPath(filePath.toStdString());
        }
        else
        {
            spdlog::warn("Cannot play: filePath is EMPTY for track '{}'", title.toStdString());
        }
    };

    libraryView->onHoverPreview = [this, provider](const juce::String& filePath,
                                                   const juce::String& hoverTitle,
                                                   const juce::String& hoverArtist) {
        if (filePath.isEmpty() || !m_nowPlayingBar) return;
        // Label fallback : filename so the NowPlayingBar is never empty.
        juce::String shownTitle  = hoverTitle.isNotEmpty() ? hoverTitle
                                   : juce::File(filePath).getFileNameWithoutExtension();
        juce::String shownArtist = hoverArtist;
        double       shownBpm = 0.0;
        juce::String shownKey;
        if (provider) {
            auto hits = provider->searchTracks(filePath.toStdString());
            for (const auto& t : hits) {
                if (juce::String(t.filePath) == filePath) {
                    if (!t.title.empty())  shownTitle  = juce::String(t.title);
                    if (!t.artist.empty()) shownArtist = juce::String(t.artist);
                    shownBpm = t.bpm;
                    shownKey = juce::String(t.camelotKey.empty() ? t.key : t.camelotKey);
                    break;
                }
            }
        }
        m_owner.updateNowPlaying(shownTitle, shownArtist, shownBpm, shownKey);
        m_nowPlayingBar->startHoverPreview(filePath);
    };
    libraryView->onOpenInHotCues = [this](int64_t trackId) {
        navigateTo(MainWindow::Nav_HotCues);
        if (auto* hv = dynamic_cast<HotCueView*>(m_views[MainWindow::Nav_HotCues].get()))
            hv->loadTrackById(trackId);
    };
    libraryView->onAnalyzeTracks = [this, provider](std::vector<int64_t> ids) {
        if (!provider || !m_listenerStorage || !m_listenerStorage->analysisListener)
            return;
        std::vector<Models::Track> tracks;
        for (auto id : ids) {
            auto t = provider->getTrack(id);
            if (t.id == id && !t.filePath.empty())
                tracks.push_back(std::move(t));
        }
        if (tracks.empty())
            return;
        const int count = static_cast<int>(tracks.size());
        m_listenerStorage->analysisListener->runAnalysis(std::move(tracks));
        Widgets::ToastNotifier::getInstance().show(
            juce::String(count) + " " + BM_TJ("library.menu.analyzeStarted"), {},
            Widgets::ToastNotifier::Kind::Info, 4000);
    };
    libraryView->onNormalizeTracks = [this](std::vector<int64_t> ids) {
        navigateTo(MainWindow::Nav_Normalization);
        if (auto* nv = dynamic_cast<NormalizationView*>(m_views[MainWindow::Nav_Normalization].get()))
            nv->queueTracks(ids);
    };
    libraryView->onAddToSet = [this](std::vector<int64_t> ids) {
        if (ids.empty())
            return;
        navigateTo(MainWindow::Nav_SetPreparation);
        if (auto* hub = dynamic_cast<PreparationHubView*>(m_views[MainWindow::Nav_SetPreparation].get())) {
            hub->setMode(true);
            if (auto* lv = hub->liveView())
                lv->addTracksFromLibrary(ids);
        }
        Widgets::ToastNotifier::getInstance().show(
            juce::String((int)ids.size()) + " " + BM_TJ("setPrep.toast.added"), {},
            Widgets::ToastNotifier::Kind::Success, 3500);
    };
    libraryView->onHoverPreviewStop = [this]() {
        if (m_nowPlayingBar)
            m_nowPlayingBar->stopHoverPreview();
    };

    m_views[MainWindow::Nav_Library]             = std::move(libraryView);
    spdlog::info("[MainWindow] ctor step: Library built");
    m_views[MainWindow::Nav_Import]              = std::make_unique<ImportView>(provider);
    spdlog::info("[MainWindow] ctor step: Import built");
    m_views[MainWindow::Nav_Analysis]            = std::make_unique<AnalysisView>(provider);
    spdlog::info("[MainWindow] ctor step: Analysis built");
    m_views[MainWindow::Nav_HotCues]             = std::make_unique<HotCueView>(provider);
    spdlog::info("[MainWindow] ctor step: HotCues built");
    m_views[MainWindow::Nav_Normalization]       = std::make_unique<NormalizationView>(provider);
    spdlog::info("[MainWindow] ctor step: Normalization built");
    m_views[MainWindow::Nav_SetPreparation]      = std::make_unique<PreparationHubView>(provider);
    spdlog::info("[MainWindow] ctor step: SetPreparation built");
    m_views[MainWindow::Nav_SoireePreparation]   = std::make_unique<SoireePreparationView>(provider);
    spdlog::info("[MainWindow] ctor step: SoireePreparation built");
    m_views[MainWindow::Nav_Agenda]              = std::make_unique<AgendaView>(provider);
    m_views[MainWindow::Nav_Compare]             = std::make_unique<ComparisonView>(provider);
    spdlog::info("[MainWindow] ctor step: Agenda built");
    // BeatMate Live is launched as an external window, not embedded
    m_views[MainWindow::Nav_Export]              = std::make_unique<ExportView>(provider);
    spdlog::info("[MainWindow] ctor step: Export built");
    m_views[MainWindow::Nav_Streaming]           = std::make_unique<StreamingView>(provider);
    spdlog::info("[MainWindow] ctor step: Streaming built");
    m_views[MainWindow::Nav_Settings]            = std::make_unique<SettingsView>();
    spdlog::info("[MainWindow] ctor step: Settings built");
    m_views[MainWindow::Nav_Help]                = std::make_unique<HelpView>();
    spdlog::info("[MainWindow] ctor step: Help built");

    if (auto* pool = BeatMate::getBackgroundPool(); pool && provider) {
        pool->addJob([provider]() {
            Services::Library::TrackRelinkService relink(provider);
            const auto missing = relink.findMissingTracks();
            if (missing.empty()) return;
            const int n = (int) missing.size();
            juce::MessageManager::callAsync([n]() {
                BeatMate::UI::Widgets::ToastNotifier::getInstance().show(
                    juce::String(n) + " fichier(s) de la bibliotheque introuvable(s)",
                    "Clic droit dans la Bibliotheque > Relier les fichiers manquants",
                    BeatMate::UI::Widgets::ToastNotifier::Kind::Warning, 9000);
            });
        });
    }

    // Views are managed through the viewport - do not add as direct children

    spdlog::info("[MainWindow] ctor step: ListenerStorage about to create");
    m_listenerStorage = std::make_unique<ListenerStorage>();
    spdlog::info("[MainWindow] ctor step: ListenerStorage created");

    if (auto* homeView = dynamic_cast<HomeView*>(m_views[MainWindow::Nav_Home].get()))
    {
        m_listenerStorage->homeListener = std::make_unique<ListenerStorage::HomeListener>(*this);
        homeView->addListener(m_listenerStorage->homeListener.get());

        if (homeView->m_recentAddedCard)
        {
            homeView->m_recentAddedCard->onTrackClicked = [this, provider](const juce::String& path,
                                                                  const juce::String& title,
                                                                  const juce::String& artist) {
                if (m_nowPlayingBar && path.isNotEmpty()) {
                    m_owner.updateNowPlaying(title, artist, 0.0, "-");
                    m_nowPlayingBar->loadAndPlay(path, title, artist);
                    if (provider)
                        provider->recordPlayByPath(path.toStdString());
                }
            };
        }

        if (homeView->m_suggestionsCard)
        {
            homeView->m_suggestionsCard->onTrackClicked = [this](const juce::String& title,
                                                                  const juce::String& artist) {
                spdlog::info("[Home] Suggestion clicked: {} - {}", title.toStdString(), artist.toStdString());
            };
        }
    }

    spdlog::info("[MainWindow] wire step: HomeView done");
    if (auto* importView = dynamic_cast<ImportView*>(m_views[MainWindow::Nav_Import].get()))
    {
        m_listenerStorage->importListener = std::make_unique<ListenerStorage::ImportListener>(provider, importView);
        importView->addListener(m_listenerStorage->importListener.get());
        importView->onNavigateToLibrary = [this] { navigateTo(MainWindow::Nav_Library); };
    }

    spdlog::info("[MainWindow] wire step: ImportView done");
    if (auto* analysisView = dynamic_cast<AnalysisView*>(m_views[MainWindow::Nav_Analysis].get()))
    {
        m_listenerStorage->analysisListener = std::make_unique<ListenerStorage::AnalysisListener>(provider, analysisView);
        analysisView->addListener(m_listenerStorage->analysisListener.get());
    }

    if (m_listenerStorage->importListener && m_listenerStorage->analysisListener)
    {
        auto* al = m_listenerStorage->analysisListener.get();
        m_listenerStorage->importListener->onAutoAnalyze =
            [al](std::vector<Models::Track> tracks) {
                al->runAnalysis(std::move(tracks));
            };
    }

    if (m_listenerStorage->analysisListener)
    {
        juce::Component::SafePointer<ContentComponent> safeContent(this);
        m_listenerStorage->analysisListener->onBadgeProgress =
            [safeContent](int done, int total) {
                auto* c = safeContent.getComponent();
                if (!c || !c->m_navModel)
                    return;
                c->m_navModel->m_analysisDone = done;
                c->m_navModel->m_analysisTotal = total;
                if (c->m_navList)
                    c->m_navList->repaint();
                if (auto* lv = dynamic_cast<LibraryView*>(c->m_views[MainWindow::Nav_Library].get()))
                    lv->refreshVisibleRowsInPlace();
            };
    }

    spdlog::info("[MainWindow] wire step: AnalysisView done");
    if (auto* exportView = dynamic_cast<ExportView*>(m_views[MainWindow::Nav_Export].get()))
    {
        m_listenerStorage->exportListener = std::make_unique<ListenerStorage::ExportListener>(provider, exportView);
        exportView->addListener(m_listenerStorage->exportListener.get());
    }

    spdlog::info("[MainWindow] wire step: ExportView done");
    if (auto* hotCueView = dynamic_cast<HotCueView*>(m_views[MainWindow::Nav_HotCues].get()))
    {
        m_listenerStorage->hotCueListener = std::make_unique<ListenerStorage::HotCueListener>(provider);
        m_listenerStorage->hotCueListener->view = hotCueView;
        hotCueView->addListener(m_listenerStorage->hotCueListener.get());

        hotCueView->onLoadTrack = [this, provider](const juce::String& path) {
            if (!m_nowPlayingBar) return;
            juce::String title, artist;
            if (provider) {
                auto hits = provider->searchTracks(path.toStdString());
                for (const auto& t : hits) {
                    if (juce::String(t.filePath) == path) {
                        title  = juce::String(t.title);
                        artist = juce::String(t.artist);
                        break;
                    }
                }
            }
            if (title.isEmpty())
                title = juce::File(path).getFileNameWithoutExtension();
            m_owner.updateNowPlaying(title, artist, 0.0, "-");
            m_nowPlayingBar->loadAndPlay(path, title, artist);
            if (provider)
                provider->recordPlayByPath(path.toStdString());
        };

        hotCueView->onPlayPause = [audioPlayer, streamingPlayer]() {
            if (streamingPlayer && streamingPlayer->getDuration() > 0.0) {
                if (streamingPlayer->isPlaying()) streamingPlayer->pause();
                else streamingPlayer->play();
                return;
            }
            if (!audioPlayer) return;
            if (audioPlayer->isPlaying())
                audioPlayer->pause();
            else
                audioPlayer->play();
        };

        hotCueView->onGetPosition = [audioPlayer, streamingPlayer]() -> double {
            if (streamingPlayer && streamingPlayer->getDuration() > 0.0)
                return streamingPlayer->getPosition();
            return audioPlayer ? audioPlayer->getPosition() : 0.0;
        };
        hotCueView->onGetDuration = [audioPlayer, streamingPlayer]() -> double {
            if (streamingPlayer && streamingPlayer->getDuration() > 0.0)
                return streamingPlayer->getDuration();
            return audioPlayer ? audioPlayer->getDuration() : 0.0;
        };

        hotCueView->onSeek = [audioPlayer, streamingPlayer](double seconds) {
            if (streamingPlayer && streamingPlayer->getDuration() > 0.0) {
                streamingPlayer->setPosition(seconds);
            } else if (audioPlayer) {
                audioPlayer->setPosition(seconds);
            }
        };

        hotCueView->onPreviewCue = [audioPlayer, streamingPlayer](double positionSeconds) {
            if (streamingPlayer && streamingPlayer->getDuration() > 0.0) {
                streamingPlayer->setPosition(positionSeconds);
                streamingPlayer->play();
            } else if (audioPlayer) {
                audioPlayer->setPosition(positionSeconds);
                if (!audioPlayer->isPlaying())
                    audioPlayer->play();
            }
        };

        hotCueView->onStopPreview = [audioPlayer, streamingPlayer]() {
            if (streamingPlayer && streamingPlayer->getDuration() > 0.0) {
                streamingPlayer->pause();
                return;
            }
            if (audioPlayer)
                audioPlayer->pause();
        };
    }

    spdlog::info("[MainWindow] wire step: HotCueView done");
    if (auto* normView = dynamic_cast<NormalizationView*>(m_views[MainWindow::Nav_Normalization].get()))
    {
        m_listenerStorage->normalizationListener = std::make_unique<ListenerStorage::NormalizationListener>(provider, normView);
        normView->addListener(m_listenerStorage->normalizationListener.get());
    }

    spdlog::info("[MainWindow] wire step: NormalizationView done");
    if (auto* hub = dynamic_cast<PreparationHubView*>(m_views[MainWindow::Nav_SetPreparation].get()))
    {
        if (auto* setView = hub->liveView())
        {
            m_listenerStorage->setPrepListener = std::make_unique<ListenerStorage::SetPrepListener>();
            setView->addListener(m_listenerStorage->setPrepListener.get());

            setView->onTrackPreview = [this, provider](const juce::String& filePath,
                                              const juce::String& title,
                                              const juce::String& artist) {
                if (m_nowPlayingBar && filePath.isNotEmpty()) {
                    m_owner.updateNowPlaying(title, artist, 0.0, "-");
                    m_nowPlayingBar->loadAndPlay(filePath, title, artist);
                    if (provider)
                        provider->recordPlayByPath(filePath.toStdString());
                }
            };
        }

        if (auto* plView = hub->playlistView())
        {
            m_listenerStorage->playlistPrepListener = std::make_unique<ListenerStorage::PlaylistPrepListener>(provider);
            plView->addListener(m_listenerStorage->playlistPrepListener.get());
        }
    }

    spdlog::info("[MainWindow] wire step: PreparationHubView done");
    spdlog::info("[MainWindow] wire step: SoireePreparationView start");
    if (auto* soireeView = dynamic_cast<SoireePreparationView*>(m_views[MainWindow::Nav_SoireePreparation].get()))
    {
        spdlog::info("[MainWindow] wire step: SoireePreparationView dynamic_cast OK");
        m_listenerStorage->soireePrepListener = std::make_unique<ListenerStorage::SoireePrepListener>(provider);
        spdlog::info("[MainWindow] wire step: SoireePrepListener constructed");
        soireeView->addListener(m_listenerStorage->soireePrepListener.get());
        spdlog::info("[MainWindow] wire step: SoireePreparationView addListener OK");
    } else {
        spdlog::warn("[MainWindow] wire step: SoireePreparationView dynamic_cast FAILED");
    }
    spdlog::info("[MainWindow] wire step: SoireePreparationView done");

    spdlog::info("[MainWindow] All view listeners wired successfully");
}

void MainWindow::ContentComponent::navigateTo(int index)
{
    {
        extern BeatMate::ServiceLocator* g_serviceLocator;
        if (g_serviceLocator) {
            auto* licSvc = g_serviceLocator->tryGet<Services::Security::LicenseService>();
            if (licSvc && licSvc->isTrialExpired()
                && index != static_cast<int>(MainWindow::Nav_Settings)
                && index != static_cast<int>(MainWindow::Nav_Help))
            {
                // Blocage navigation en fin d'essai désactivé volontairement.
                spdlog::info("[MainWindow] navigateTo: trial flagged expired but gating disabled — letting nav proceed.");
            }
        }
    }

    // Agenda : reserve aux versions payantes.
    if (index == static_cast<int>(MainWindow::Nav_Agenda))
    {
        extern BeatMate::ServiceLocator* g_serviceLocator;
        if (g_serviceLocator) {
            if (auto* licSvc = g_serviceLocator->tryGet<Services::Security::LicenseService>()) {
                if (! licSvc->canUseAgenda()) {
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                        juce::String::fromUTF8("Agenda"),
                        juce::String::fromUTF8(
                            "L'Agenda (dates, cachets, rappels, exports PDF, Word et .ics) "
                            "fait partie des versions payantes de BeatMate.\n\n"
                            "Activez votre licence dans Param\xc3\xa8tres > Licence pour y acc\xc3\xa9""der."),
                        BM_TJ("common.ok"));
                    return;
                }
            }
        }
    }

    // Mix Studio : lance en fenetre separee (process bundle), comme Live.
    if (index == static_cast<int>(MainWindow::Nav_Mix))
    {
        {
            extern BeatMate::ServiceLocator* g_serviceLocator;
            if (g_serviceLocator) {
                if (auto* licSvc = g_serviceLocator->tryGet<Services::Security::LicenseService>()) {
                    if (!licSvc->canUseStudio()) {
                        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                            juce::String::fromUTF8("Mix Studio"),
                            juce::String::fromUTF8("Mix Studio necessite la version Professional ou Premium."),
                            BM_TJ("common.ok"));
                        return;
                    }
                }
            }
        }

        const juce::String exeName("BeatMate Mix Studio.exe");
        const juce::String folder("Mix Studio");

        const auto exeDir = juce::File::getSpecialLocation(juce::File::currentApplicationFile)
                                .getParentDirectory();
        juce::StringArray candidates;
        candidates.add(exeDir.getParentDirectory().getChildFile(folder).getChildFile(exeName).getFullPathName());
        candidates.add(exeDir.getChildFile(exeName).getFullPathName());
#ifndef NDEBUG
        // Repli poste de developpement : jamais compile dans les binaires livres.
        candidates.add(juce::File::getCurrentWorkingDirectory()
                           .getChildFile("MixStudio/build/MixStudio_artefacts/Release")
                           .getChildFile(exeName).getFullPathName());
#endif

        juce::File target;
        for (const auto& c : candidates) { juce::File f(c); if (f.existsAsFile()) { target = f; break; } }

        if (target.existsAsFile())
        {
            spdlog::info("[MainWindow] {} -> launching {}", exeName.toStdString(),
                         target.getFullPathName().toStdString());
            target.startAsProcess();
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, exeName,
                juce::String::fromUTF8("Module introuvable. Reinstallez la suite BeatMate."),
                BM_TJ("common.ok"));
        }
        return;
    }

    // PerfDJ retire de V12 (Nav_PerfDJ supprime de la navigation).

    if (index == static_cast<int>(MainWindow::Nav_BeatMateLive))
    {
        {
            extern BeatMate::ServiceLocator* g_serviceLocator;
            if (g_serviceLocator) {
                auto* licSvc = g_serviceLocator->tryGet<Services::Security::LicenseService>();
                if (licSvc && !licSvc->canUseLive()) {
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                        juce::String::fromUTF8("Live"),
                        juce::String::fromUTF8("Live necessite la version Premium."),
                        BM_TJ("common.ok"));
                    return;
                }
            }
        }

        if (!m_beatMateLiveWindow)
        {
            extern BeatMate::ServiceLocator* g_serviceLocator;
            Services::Library::TrackDataProvider* liveProvider = nullptr;
            if (g_serviceLocator)
                liveProvider = g_serviceLocator->tryGet<Services::Library::TrackDataProvider>();
            m_beatMateLiveWindow = std::make_unique<BeatMateLiveWindow>(liveProvider);

            // Send-to-Prep : double-clic sur une suggestion → Set Preparation.
            if (auto* liveView = dynamic_cast<BeatMateLiveView*>(m_beatMateLiveWindow->getContentComponent()))
            {
                liveView->onSendSuggestionToPrep =
                    [this, liveProvider](const BeatMateLiveView::SuggestionPayload& p) {
                        if (!liveProvider) {
                            spdlog::warn("[Live] Send-to-Prep: no TrackDataProvider");
                            return;
                        }
                        // Find the best matching track : file path first, then title+artist.
                        int64_t matchedId = -1;
                        if (p.filePath.isNotEmpty()) {
                            auto all = liveProvider->getAllTracks();
                            for (const auto& t : all) {
                                if (t.filePath == p.filePath.toStdString()) {
                                    matchedId = t.id;
                                    break;
                                }
                            }
                        }
                        if (matchedId < 0) {
                            auto hits = liveProvider->searchTracks(
                                (p.title + " " + p.artist).toStdString());
                            if (!hits.empty()) matchedId = hits.front().id;
                        }
                        if (matchedId < 0) {
                            spdlog::info("[Live] Send-to-Prep: '{}' not in library",
                                          p.title.toStdString());
                            juce::MessageManager::callAsync([title = p.title]() {
                                juce::AlertWindow::showMessageBoxAsync(
                                    juce::MessageBoxIconType::InfoIcon,
                                    BM_TJ("mainWindow.trackNotFound"),
                                    juce::String::fromUTF8("\xc2\xab ") + title +
                                        juce::String::fromUTF8(" \xc2\xbb ") + BM_TJ("mainWindow.trackNotFoundMsg"),
                                    BM_TJ("common.ok"));
                            });
                            return;
                        }
                        const int64_t finalId = matchedId;
                        juce::MessageManager::callAsync([this, finalId, title = p.title]() {
                            auto it = m_views.find(static_cast<int>(MainWindow::Nav_SetPreparation));
                            if (it != m_views.end()) {
                                if (auto* hub = dynamic_cast<PreparationHubView*>(it->second.get())) {
                                    hub->setMode(true);
                                    hub->liveView()->addTracksFromLibrary({ finalId });
                                    spdlog::info("[Live] Send-to-Prep: added '{}' (id={})",
                                                  title.toStdString(), finalId);
                                    return;
                                }
                            }
                            spdlog::warn("[Live] Send-to-Prep: SetPreparationView unavailable");
                        });
                    };
            }
        }

        // Ouvrir Live masque immédiatement la fenêtre principale.
        if (auto* mainWindow = findParentComponentOfClass<juce::DocumentWindow>()) {
            mainWindow->setMinimised(true);
            mainWindow->setVisible(false);
        }
        m_beatMateLiveWindow->expandToFull();

        m_beatMateLiveWindow->onCloseCallback = [this]() {
            // X on Live destroys Live entirely and brings the main window back.
            if (auto* mainWindow = findParentComponentOfClass<juce::DocumentWindow>()) {
                mainWindow->setVisible(true);
                mainWindow->setMinimised(false);
                mainWindow->toFront(true);
            }
            juce::Component::SafePointer<juce::Component> safe(this);
            juce::MessageManager::callAsync([safe]() {
                if (auto* content = dynamic_cast<ContentComponent*>(safe.getComponent()))
                    content->m_beatMateLiveWindow.reset();
            });
        };

        // Keep current view selected, don't switch - find row matching current view
        for (int row = 0; row < static_cast<int>(m_navModel->m_items.size()); ++row)
        {
            if (m_navModel->m_items[static_cast<size_t>(row)].navTarget == m_currentViewIndex)
            {
                m_navList->selectRow(row);
                break;
            }
        }
        return;
    }

    m_currentViewIndex = index;

    auto t0 = juce::Time::getMillisecondCounterHiRes();

    auto it = m_views.find(index);
    if (it != m_views.end())
    {
        spdlog::warn("[Nav] Switching to view {} ptr={}", index, (void*)it->second.get());
        spdlog::default_logger()->flush();
        m_viewport->setViewedComponent(it->second.get(), false);
        m_viewport->setViewPosition(0, 0);
    } else {
        spdlog::error("[Nav] View {} NOT FOUND in m_views!", index);
        spdlog::default_logger()->flush();
    }

    auto t1 = juce::Time::getMillisecondCounterHiRes();
    spdlog::info("[Nav] Switch to view {} took {:.1f}ms", index, t1 - t0);

    // Force immediate resize to avoid layout delay
    resized();

    for (int row = 0; row < static_cast<int>(m_navModel->m_items.size()); ++row)
    {
        if (m_navModel->m_items[static_cast<size_t>(row)].navTarget == index)
        {
            m_navList->selectRow(row);
            break;
        }
    }

    for (auto& [btn, target] : m_toolbarTargets)
        if (btn)
            btn->setColour(juce::TextButton::textColourOffId,
                           target == index ? moduleAccentFor(target) : Colors::textSecondary());

    resized();

    if (it != m_views.end() && it->second)
        it->second->setAlpha(1.0f);
}


bool MainWindow::ContentComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress('i', juce::ModifierKeys::ctrlModifier, 0)) {
        navigateTo(MainWindow::Nav_Import); return true;
    }
    if (key == juce::KeyPress('f', juce::ModifierKeys::ctrlModifier, 0)) {
        navigateTo(MainWindow::Nav_Library); return true;
    }
    if (key == juce::KeyPress('e', juce::ModifierKeys::ctrlModifier, 0)) {
        navigateTo(MainWindow::Nav_Export); return true;
    }
    if (key == juce::KeyPress('l', juce::ModifierKeys::ctrlModifier, 0)) {
        navigateTo(MainWindow::Nav_BeatMateLive); return true;
    }
    if (key == juce::KeyPress(juce::KeyPress::F1Key)) {
        navigateTo(MainWindow::Nav_Help); return true;
    }
    if (key == juce::KeyPress(juce::KeyPress::F5Key)) {
        auto it = m_views.find(m_currentViewIndex);
        if (it != m_views.end() && it->second)
            it->second->repaint();
        return true;
    }
    return false;
}

void MainWindow::ContentComponent::paint(juce::Graphics& g)
{
    g.fillAll(Colors::bg());

    {
        juce::ColourGradient sidebarGrad(Colors::bgSidebar().darker(0.15f), 0.0f, 0.0f,
                                          Colors::bgSidebar().brighter(0.08f), 0.0f, static_cast<float>(getHeight()), false);
        g.setGradientFill(sidebarGrad);
        g.fillRect(0, 0, kSidebarWidth, getHeight());
    }

    {
        juce::ColourGradient leftGlow(Colors::primary().withAlpha(0.05f), 0.0f, 0.0f,
                                       juce::Colours::transparentBlack, 2.0f, 0.0f, false);
        g.setGradientFill(leftGlow);
        g.fillRect(0, 0, 2, getHeight());
    }

    {
        float h = static_cast<float>(getHeight());
        juce::ColourGradient borderGrad(juce::Colours::transparentBlack, static_cast<float>(kSidebarWidth), 0.0f,
                                         juce::Colours::transparentBlack, static_cast<float>(kSidebarWidth), h, false);
        borderGrad.addColour(0.1, Colors::border().withAlpha(0.3f));
        borderGrad.addColour(0.5, Colors::border());
        borderGrad.addColour(0.9, Colors::border().withAlpha(0.3f));
        g.setGradientFill(borderGrad);
        g.fillRect(kSidebarWidth, 0, 1, getHeight());
    }

    {
        float cx = 30.0f, cy = 26.0f, r = 14.0f;

        g.setColour(Colors::primary().withAlpha(0.15f));
        g.fillEllipse(cx - r - 4, cy - r - 4, (r + 4) * 2, (r + 4) * 2);

        juce::ColourGradient discGrad(Colors::primary(), cx - r, cy - r,
            juce::Colour(0xFF8B5CF6), cx + r, cy + r, true);
        g.setGradientFill(discGrad);
        g.fillEllipse(cx - r, cy - r, r * 2, r * 2);

        g.setColour(juce::Colour(0xFF0C0C14));
        g.fillEllipse(cx - 4, cy - 4, 8, 8);
        g.setColour(Colors::primary().brighter(0.3f));
        g.fillEllipse(cx - 2, cy - 2, 4, 4);

        g.setColour(juce::Colours::white.withAlpha(0.8f));
        for (int i = 1; i <= 3; ++i) {
            float wr = r + 2.0f + i * 3.5f;
            juce::Path arc;
            arc.addCentredArc(cx, cy, wr, wr, 0.0f, -0.5f, 0.5f, true);
            g.strokePath(arc, juce::PathStrokeType(1.2f));
        }
    }

    {
        float sw = static_cast<float>(kSidebarWidth);
        juce::ColourGradient logoSepGrad(juce::Colours::transparentBlack, 0.0f, 52.0f,
                                          juce::Colours::transparentBlack, sw, 52.0f, false);
        logoSepGrad.addColour(0.15, Colors::border());
        logoSepGrad.addColour(0.85, Colors::border());
        g.setGradientFill(logoSepGrad);
        g.fillRect(0, 52, kSidebarWidth, 1);
    }

    auto drawSeparator = [&](int afterRowIndex) {
        int sepY = 53 + (afterRowIndex + 1) * 44;
        float sw = static_cast<float>(kSidebarWidth);
        juce::ColourGradient sepGrad(juce::Colours::transparentBlack, 12.0f, static_cast<float>(sepY),
                                      juce::Colours::transparentBlack, sw - 12.0f, static_cast<float>(sepY), false);
        sepGrad.addColour(0.15, Colors::border().withAlpha(0.6f));
        sepGrad.addColour(0.5, Colors::border());
        sepGrad.addColour(0.85, Colors::border().withAlpha(0.6f));
        g.setGradientFill(sepGrad);
        g.fillRect(12, sepY, kSidebarWidth - 24, 1);
    };
    drawSeparator(5);   // After Normalisation (index 5)
    drawSeparator(10);  // After Export (index 10)
    drawSeparator(12);  // After Streaming (index 12)

    g.setColour(Colors::bgSurface());
    g.fillRect(kSidebarWidth + 1, 0, getWidth() - kSidebarWidth - 1, kToolbarHeight);
    g.setColour(juce::Colour(0xFF1E293B));
    g.drawHorizontalLine(kToolbarHeight, static_cast<float>(kSidebarWidth + 1), static_cast<float>(getWidth()));

    int statusBarY = getHeight() - kStatusBarHeight;
    {
        juce::ColourGradient statusGrad(Colors::bgSidebar().darker(0.1f), 0.0f, static_cast<float>(statusBarY),
                                         Colors::bgSidebar().brighter(0.05f), static_cast<float>(getWidth()), static_cast<float>(statusBarY), false);
        g.setGradientFill(statusGrad);
        g.fillRect(0, statusBarY, getWidth(), kStatusBarHeight);
    }
    {
        juce::ColourGradient statusBorderGrad(juce::Colours::transparentBlack, 0.0f, static_cast<float>(statusBarY),
                                               juce::Colours::transparentBlack, static_cast<float>(getWidth()), static_cast<float>(statusBarY), false);
        statusBorderGrad.addColour(0.05, Colors::border());
        statusBorderGrad.addColour(0.95, Colors::border());
        g.setGradientFill(statusBorderGrad);
        g.fillRect(0, statusBarY, getWidth(), 1);
    }

    {
        auto r = m_cpuBarRect.toFloat();
        const float fill = juce::jlimit(0.0f, 1.0f, m_cpuFill);
        g.setColour(Colors::bgElevated());
        g.fillRoundedRectangle(r, 2.0f);
        juce::ColourGradient cpuGrad(Colors::accent(), r.getX(), r.getY(),
                                      Colors::primary(), r.getRight(), r.getY(), false);
        g.setGradientFill(cpuGrad);
        g.fillRoundedRectangle(r.withWidth(fill * r.getWidth()), 2.0f);
    }
    {
        auto r = m_ramBarRect.toFloat();
        const float fill = juce::jlimit(0.0f, 1.0f, m_ramFill);
        g.setColour(Colors::bgElevated());
        g.fillRoundedRectangle(r, 2.0f);
        auto ramColor = fill < 0.6f ? Colors::success() : (fill < 0.8f ? Colors::warning() : Colors::error());
        g.setColour(ramColor);
        g.fillRoundedRectangle(r.withWidth(fill * r.getWidth()), 2.0f);
    }

    {
        float dotY = static_cast<float>(statusBarY + kStatusBarHeight / 2);
        float dotR = 1.5f;
        g.setColour(Colors::textDim());
        g.fillEllipse(static_cast<float>(m_statusCPU->getX() - 10), dotY - dotR, dotR * 2, dotR * 2);
        g.fillEllipse(static_cast<float>(m_statusRAM->getX() - 10), dotY - dotR, dotR * 2, dotR * 2);
        g.fillEllipse(static_cast<float>(m_statusLatency->getX() - 10), dotY - dotR, dotR * 2, dotR * 2);
    }

    for (auto* tb : { m_tbImport.get(), m_tbAnalyze.get(), m_tbHotCues.get(),
                      m_tbNormalize.get(), m_tbExport.get() })
    {
        if (!tb) continue;
        const int target = m_toolbarTargets.count(tb) ? m_toolbarTargets.at(tb) : -1;
        if (target != m_currentViewIndex) continue;
        auto r = tb->getBounds();
        g.setColour(moduleAccentFor(target));
        g.fillRoundedRectangle(static_cast<float>(r.getX() + 8),
                               static_cast<float>(kToolbarHeight - 5),
                               static_cast<float>(r.getWidth() - 16), 2.5f, 1.25f);
    }
}

void MainWindow::ContentComponent::resized()
{
    auto bounds = getLocalBounds();

    m_logoLabel->setBounds(48, 8, kSidebarWidth - 52, 36);

    int navTop = 53;
    int navBottom = bounds.getHeight() - kStatusBarHeight;
    m_navList->setBounds(0, navTop, kSidebarWidth, navBottom - navTop);

    int tbX = kSidebarWidth + 12;
    int tbY = 5;
    int tbW = 84;
    int tbH = kToolbarHeight - 10;

    m_tbImport->setBounds(tbX, tbY, tbW, tbH);     tbX += tbW + 6;
    m_tbAnalyze->setBounds(tbX, tbY, tbW, tbH);    tbX += tbW + 6;
    m_tbHotCues->setBounds(tbX, tbY, tbW, tbH);    tbX += tbW + 6;
    m_tbNormalize->setBounds(tbX, tbY, tbW + 10, tbH); tbX += tbW + 16;
    m_tbExport->setBounds(tbX, tbY, tbW, tbH);     tbX += tbW + 6;
    if (m_tbAgenda) m_tbAgenda->setBounds(tbX, tbY, tbW, tbH);

    if (m_agendaReminderBtn)
    {
        m_lastReminderCheckMs = 0;
        updateAgendaReminder();
    }

    int viewLeft = kSidebarWidth + 1;
    int viewTop = kToolbarHeight + 1;
    int viewRight = bounds.getWidth();
    int viewBottom = bounds.getHeight() - kNowPlayingHeight - kStatusBarHeight - 4;
    int viewW = viewRight - viewLeft;
    int viewH = viewBottom - viewTop;

    m_viewport->setBounds(viewLeft, viewTop, viewW, viewH);

    auto it = m_views.find(m_currentViewIndex);
    if (it != m_views.end() && it->second)
    {
        int minViewH = juce::jmax(viewH, 800);
        it->second->setSize(viewW - (m_viewport->isVerticalScrollBarShown() ? 8 : 0), minViewH);
    }

    constexpr int kFavoritesHeight = 28;
    int favY = bounds.getHeight() - kNowPlayingHeight - kStatusBarHeight - kFavoritesHeight;
    if (m_favoritesBar)
        m_favoritesBar->setBounds(kSidebarWidth + 1, favY,
                                  bounds.getWidth() - kSidebarWidth - 1, kFavoritesHeight);

    int npY = bounds.getHeight() - kNowPlayingHeight - kStatusBarHeight;
    m_nowPlayingBar->setBounds(kSidebarWidth + 1, npY, bounds.getWidth() - kSidebarWidth - 1, kNowPlayingHeight);

    int sbY = bounds.getHeight() - kStatusBarHeight;
    int sbX = 8;
    m_statusDJSoftware->setBounds(sbX, sbY, 150, kStatusBarHeight);
    sbX += 158;
    m_statusCPU->setBounds(sbX, sbY, 80, kStatusBarHeight);
    sbX += 84;
    m_cpuBarRect = juce::Rectangle<int>(sbX, sbY + kStatusBarHeight / 2 - 2, 40, 4);
    sbX += 50;
    m_statusRAM->setBounds(sbX, sbY, 100, kStatusBarHeight);
    sbX += 104;
    m_ramBarRect = juce::Rectangle<int>(sbX, sbY + kStatusBarHeight / 2 - 2, 40, 4);
    sbX += 50;
    m_statusLatency->setBounds(sbX, sbY, 130, kStatusBarHeight);
    m_statusCreator->setBounds(sbX + 140, sbY, 220, kStatusBarHeight);
    m_statusTrack->setBounds(bounds.getWidth() - 250, sbY, 240, kStatusBarHeight);

    // Toast overlay covers the whole client area; hitTest gates the clicks.
    if (m_toastNotifier) {
        m_toastNotifier->setBounds(bounds);
        m_toastNotifier->toFront(false);
    }

    repaint();
}


juce::Path MainWindow::ContentComponent::NavListModel::getIconPath(int index, float cx, float cy, float sz)
{
    juce::Path p;
    float half = sz * 0.5f;

    switch (index)
    {
    case 0: // Home - house shape
    {
        p.addTriangle(cx - half, cy, cx + half, cy, cx, cy - half);
        p.addRectangle(cx - half * 0.6f, cy, half * 1.2f, half * 0.8f);
        break;
    }
    case 1: // Library - stacked books
    {
        float bw = half * 0.3f;
        p.addRectangle(cx - half, cy - half * 0.8f, bw, half * 1.6f);
        p.addRectangle(cx - half + bw * 1.3f, cy - half * 0.6f, bw, half * 1.4f);
        p.addRectangle(cx - half + bw * 2.6f, cy - half * 0.8f, bw, half * 1.6f);
        p.addRectangle(cx - half + bw * 3.9f, cy - half * 0.5f, bw, half * 1.3f);
        break;
    }
    case 2: // Import - download arrow
    {
        p.addRectangle(cx - half * 0.15f, cy - half * 0.6f, half * 0.3f, half * 0.8f);
        p.addTriangle(cx - half * 0.5f, cy + half * 0.2f, cx + half * 0.5f, cy + half * 0.2f, cx, cy + half * 0.7f);
        p.addRectangle(cx - half * 0.6f, cy + half * 0.8f, half * 1.2f, half * 0.15f);
        break;
    }
    case 3: // Analysis - magnifying glass
    {
        p.addEllipse(cx - half * 0.5f, cy - half * 0.5f, half, half);
        p.addRectangle(cx + half * 0.15f, cy + half * 0.15f, half * 0.5f, half * 0.15f);
        p.applyTransform(juce::AffineTransform::rotation(0.7f, cx + half * 0.15f, cy + half * 0.15f));
        break;
    }
    case 4: // Hot Cues - target/crosshair
    {
        p.addEllipse(cx - half * 0.6f, cy - half * 0.6f, half * 1.2f, half * 1.2f);
        p.addEllipse(cx - half * 0.25f, cy - half * 0.25f, half * 0.5f, half * 0.5f);
        break;
    }
    case 5: // Normalisation - equalizer bars
    {
        float bw2 = half * 0.2f;
        p.addRectangle(cx - half * 0.7f, cy - half * 0.3f, bw2, half * 1.0f);
        p.addRectangle(cx - half * 0.25f, cy - half * 0.7f, bw2, half * 1.4f);
        p.addRectangle(cx + half * 0.2f, cy - half * 0.1f, bw2, half * 0.8f);
        p.addRectangle(cx + half * 0.55f, cy - half * 0.5f, bw2, half * 1.2f);
        break;
    }
    case 6: // Preparation Set - music note
    {
        p.addEllipse(cx - half * 0.4f, cy + half * 0.1f, half * 0.5f, half * 0.4f);
        p.addRectangle(cx + half * 0.05f, cy - half * 0.7f, half * 0.1f, half * 0.9f);
        p.addRectangle(cx + half * 0.05f, cy - half * 0.7f, half * 0.4f, half * 0.1f);
        break;
    }
    case 7: // Preparation Soiree - calendar/moon
    {
        p.addRoundedRectangle(cx - half * 0.6f, cy - half * 0.5f, half * 1.2f, half * 1.1f, half * 0.1f);
        p.addRectangle(cx - half * 0.6f, cy - half * 0.5f, half * 1.2f, half * 0.25f);
        // star in calendar
        p.addStar(juce::Point<float>(cx, cy + half * 0.15f), 5, half * 0.15f, half * 0.3f);
        break;
    }
    case 8: // Playlists - stacked list
    {
        float lineH = half * 0.12f;
        for (int i = 0; i < 4; ++i)
        {
            float ly = cy - half * 0.4f + static_cast<float>(i) * half * 0.35f;
            p.addRoundedRectangle(cx - half * 0.6f, ly, half * 1.2f, lineH, lineH * 0.5f);
        }
        break;
    }
    case 9: // BeatMate Live - heartbeat pulse
    {
        p.startNewSubPath(cx - half * 0.8f, cy);
        p.lineTo(cx - half * 0.4f, cy);
        p.lineTo(cx - half * 0.2f, cy - half * 0.6f);
        p.lineTo(cx, cy + half * 0.4f);
        p.lineTo(cx + half * 0.2f, cy - half * 0.3f);
        p.lineTo(cx + half * 0.4f, cy);
        p.lineTo(cx + half * 0.8f, cy);
        break;
    }
    case 10: // Export - upload arrow
    {
        p.addTriangle(cx - half * 0.5f, cy - half * 0.1f, cx + half * 0.5f, cy - half * 0.1f, cx, cy - half * 0.7f);
        p.addRectangle(cx - half * 0.15f, cy - half * 0.1f, half * 0.3f, half * 0.7f);
        p.addRectangle(cx - half * 0.6f, cy + half * 0.7f, half * 1.2f, half * 0.15f);
        break;
    }
    case 11: // PerfDJ - headphones
    {
        juce::Path arc;
        arc.addArc(cx - half * 0.6f, cy - half * 0.6f, half * 1.2f, half * 1.0f,
                   juce::MathConstants<float>::pi, juce::MathConstants<float>::twoPi, true);
        p.addPath(arc);
        p.addRectangle(cx - half * 0.7f, cy - half * 0.1f, half * 0.25f, half * 0.6f);
        p.addRectangle(cx + half * 0.45f, cy - half * 0.1f, half * 0.25f, half * 0.6f);
        break;
    }
    case 12: // Streaming - globe/wifi
    {
        p.addEllipse(cx - half * 0.6f, cy - half * 0.6f, half * 1.2f, half * 1.2f);
        p.addEllipse(cx - half * 0.25f, cy - half * 0.6f, half * 0.5f, half * 1.2f);
        p.startNewSubPath(cx - half * 0.6f, cy);
        p.lineTo(cx + half * 0.6f, cy);
        break;
    }
    case 13: // Settings - gear
    {
        p.addStar(juce::Point<float>(cx, cy), 8, half * 0.35f, half * 0.6f);
        // center hole (draw as separate filled circle - will be painted separately)
        break;
    }
    case 14: // Help - question mark circle
    {
        p.addEllipse(cx - half * 0.7f, cy - half * 0.7f, half * 1.4f, half * 1.4f);
        break;
    }
    case 15: // Studio (SonicDeck) - timeline sliders
    {
        p.addRectangle(cx - half * 0.7f, cy - half * 0.45f, half * 1.4f, half * 0.18f);
        p.addRectangle(cx - half * 0.7f, cy - half * 0.05f, half * 1.4f, half * 0.18f);
        p.addRectangle(cx - half * 0.7f, cy + half * 0.35f, half * 1.4f, half * 0.18f);
        p.addEllipse(cx - half * 0.25f, cy - half * 0.52f, half * 0.32f, half * 0.32f);
        p.addEllipse(cx + half * 0.15f, cy - half * 0.12f, half * 0.32f, half * 0.32f);
        break;
    }
    case 16: // Jingle - microphone
    {
        p.addRoundedRectangle(cx - half * 0.25f, cy - half * 0.7f, half * 0.5f, half * 0.9f, half * 0.25f);
        juce::Path arc;
        arc.addArc(cx - half * 0.5f, cy - half * 0.3f, half * 1.0f, half * 0.8f,
                   0.0f, juce::MathConstants<float>::pi, true);
        p.addPath(arc);
        p.addRectangle(cx - half * 0.05f, cy + half * 0.3f, half * 0.1f, half * 0.4f);
        break;
    }
    case 17: // Mix - two overlapping decks
    {
        p.addEllipse(cx - half * 0.7f, cy - half * 0.35f, half * 0.7f, half * 0.7f);
        p.addEllipse(cx + half * 0.0f, cy - half * 0.35f, half * 0.7f, half * 0.7f);
        break;
    }
    case MainWindow::Nav_Agenda: // Agenda - calendar
    {
        p.addRoundedRectangle(cx - half * 0.7f, cy - half * 0.6f, half * 1.4f, half * 1.25f, half * 0.12f);
        p.addRectangle(cx - half * 0.7f, cy - half * 0.6f, half * 1.4f, half * 0.28f);
        p.addRectangle(cx - half * 0.4f, cy - half * 0.85f, half * 0.12f, half * 0.35f);
        p.addRectangle(cx + half * 0.28f, cy - half * 0.85f, half * 0.12f, half * 0.35f);
        break;
    }
    case MainWindow::Nav_Compare: // Comparaison - deux panneaux + flèches
    {
        p.addRoundedRectangle(cx - half * 0.85f, cy - half * 0.6f, half * 0.7f, half * 1.2f, half * 0.1f);
        p.addRoundedRectangle(cx + half * 0.15f, cy - half * 0.6f, half * 0.7f, half * 1.2f, half * 0.1f);
        p.addArrow({ cx - half * 0.1f, cy - half * 0.18f, cx + half * 0.1f, cy - half * 0.18f },
                   half * 0.1f, half * 0.26f, half * 0.14f);
        p.addArrow({ cx + half * 0.1f, cy + half * 0.18f, cx - half * 0.1f, cy + half * 0.18f },
                   half * 0.1f, half * 0.26f, half * 0.14f);
        break;
    }
    default:
        p.addEllipse(cx - half * 0.3f, cy - half * 0.3f, half * 0.6f, half * 0.6f);
        break;
    }

    return p;
}

void MainWindow::ContentComponent::NavListModel::paintListBoxItem(
    int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= static_cast<int>(m_items.size()))
        return;

    auto& item = m_items[static_cast<size_t>(rowNumber)];
    bool isHovered = (rowNumber == m_hoveredRow);
    const int navTarget = item.navTarget;
    const juce::Colour accent = moduleAccentFor(navTarget);

    if (rowIsSelected)
    {
        juce::ColourGradient selGrad(accent.withAlpha(0.14f), 0.0f, 0.0f,
                                      juce::Colours::transparentBlack, static_cast<float>(width), 0.0f, false);
        g.setGradientFill(selGrad);
        g.fillRect(0, 0, width, height);

        g.setColour(accent);
        g.fillRoundedRectangle(0.0f, 4.0f, 3.0f, static_cast<float>(height - 8), 1.5f);

        g.setColour(accent);
        g.fillEllipse(42.0f, static_cast<float>(height / 2 - 2), 4.0f, 4.0f);
    }
    else if (isHovered)
    {
        g.setColour(Colors::glassHover());
        g.fillRoundedRectangle(4.0f, 2.0f, static_cast<float>(width - 8), static_cast<float>(height - 4), 6.0f);

        juce::ColourGradient glowGrad(
            accent.withAlpha(0.10f), 0.0f, static_cast<float>(height / 2),
            juce::Colours::transparentBlack, static_cast<float>(width) * 0.7f, static_cast<float>(height / 2),
            true);
        g.setGradientFill(glowGrad);
        g.fillRoundedRectangle(2.0f, 2.0f, static_cast<float>(width - 4), static_cast<float>(height - 4), 8.0f);

        g.setColour(accent.withAlpha(0.5f));
        g.fillRoundedRectangle(0.0f, 5.0f, 3.0f, static_cast<float>(height - 10), 1.5f);
    }

    float iconCx = 26.0f;
    float iconCy = static_cast<float>(height) * 0.5f;
    float iconSz = 16.0f;
    auto iconPath = getIconPath(navTarget, iconCx, iconCy, iconSz);

    auto iconColour = rowIsSelected ? accent
                    : isHovered    ? Colors::textPrimary()
                    : Colors::textSecondary();
    if (navTarget == MainWindow::Nav_Settings)
    {
        g.setColour(iconColour);
        g.fillPath(iconPath);
        g.setColour(Colors::bgSidebar());
        g.fillEllipse(iconCx - 3.0f, iconCy - 3.0f, 6.0f, 6.0f);
    }
    else if (navTarget == MainWindow::Nav_Help)
    {
        g.setColour(iconColour);
        g.strokePath(iconPath, juce::PathStrokeType(1.5f));
        g.setFont(Fonts::uiFont(11.0f, Fonts::Weight::SemiBold));
        g.drawText("?", static_cast<int>(iconCx - 4), static_cast<int>(iconCy - 6), 8, 12, juce::Justification::centred);
    }
    else if (navTarget == MainWindow::Nav_BeatMateLive || navTarget == MainWindow::Nav_Streaming)
    {
        g.setColour(iconColour);
        g.strokePath(iconPath, juce::PathStrokeType(1.5f));
    }
    else
    {
        g.setColour(iconColour);
        g.fillPath(iconPath);
    }

    int textX = rowIsSelected ? 50 : 48;
    g.setFont(Fonts::uiFont(13.0f, rowIsSelected ? Fonts::Weight::SemiBold : Fonts::Weight::Medium));

    if (rowIsSelected)
    {
        g.setColour(accent.withAlpha(0.15f));
        g.drawText(item.text, textX - 1, -1, width - 56, height, juce::Justification::centredLeft);
        g.drawText(item.text, textX + 1, 1, width - 56, height, juce::Justification::centredLeft);
    }

    if (isHovered && !rowIsSelected)
    {
        g.setColour(accent.withAlpha(0.08f));
        g.drawText(item.text, textX - 1, 0, width - 56, height, juce::Justification::centredLeft);
        g.drawText(item.text, textX + 1, 0, width - 56, height, juce::Justification::centredLeft);
    }

    g.setColour(rowIsSelected ? Colors::textPrimary()
              : isHovered    ? Colors::textPrimary()
              : Colors::textSecondary());
    g.drawText(item.text, textX, 0, width - 56, height, juce::Justification::centredLeft);

    int badgeCount = 0;
    juce::Colour badgeCol;
    if (navTarget == MainWindow::Nav_Library && m_precachePending > 0)
    {
        badgeCount = m_precachePending;
        badgeCol = Colors::moduleLibrary();
    }
    else if (navTarget == MainWindow::Nav_Analysis && m_analysisTotal > 0)
    {
        badgeCount = juce::jmax(0, m_analysisTotal - m_analysisDone);
        badgeCol = Colors::moduleAnalysis();
    }
    if (badgeCount > 0)
    {
        const juce::String txt = badgeCount > 999 ? juce::String("999+") : juce::String(badgeCount);
        g.setFont(Fonts::monoFont(9.5f, Fonts::Weight::Medium));
        const int tw = juce::jmax(20, static_cast<int>(g.getCurrentFont().getStringWidthFloat(txt)) + 12);
        juce::Rectangle<float> pill(static_cast<float>(width - tw - 10),
                                    static_cast<float>(height / 2 - 8),
                                    static_cast<float>(tw), 16.0f);
        g.setColour(badgeCol.withAlpha(0.16f));
        g.fillRoundedRectangle(pill, 8.0f);
        g.setColour(badgeCol.withAlpha(0.9f));
        g.drawText(txt, pill.toNearestInt(), juce::Justification::centred);
    }
}

void MainWindow::ContentComponent::NavListModel::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < static_cast<int>(m_items.size()))
    {
        int navTarget = m_items[static_cast<size_t>(row)].navTarget;
        m_owner.navigateTo(navTarget);
        m_owner.m_owner.m_listeners.call([navTarget](MainWindow::Listener& l) { l.navigationChanged(navTarget); });
    }
}

} // namespace BeatMate::UI
