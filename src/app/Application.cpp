#include "Application.h"
#include "ServiceLocator.h"
#include "Config.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/FirstTimeSetupDialog.h"
#include <thread>
#include <set>

#include "core/audio/AudioEngine.h"
#include "core/audio/AudioDeviceManager.h"
#include "core/audio/AudioPlayer.h"
#include "core/audio/StreamingPlayer.h"
#include "core/audio/AudioMixer.h"
#include "core/audio/AudioFileReader.h"
#include "core/audio/AudioFileWriter.h"
#include "core/audio/AudioPreview.h"
#include "core/analysis/BPMDetector.h"
#include "core/analysis/HybridBPMDetector.h"
#include "core/analysis/KeyDetector.h"
#include "core/analysis/HybridKeyDetector.h"
#include "core/analysis/EnergyAnalyzer.h"
#include "core/analysis/LoudnessAnalyzer.h"
#include "core/analysis/StructureDetector.h"
#include "core/analysis/PhraseDetector.h"
#include "core/analysis/SectionDetector.h"
#include "core/analysis/OnsetDetector.h"
#include "core/analysis/SpectrumAnalyzer.h"
#include "core/analysis/WaveformGenerator.h"
#include "core/analysis/MultiBandWaveform.h"
#include "core/analysis/BeatGridGenerator.h"
#include "core/analysis/BeatGridEditor.h"
#include "core/analysis/GenreClassifier.h"
#include "core/analysis/MoodDetector.h"
#include "core/analysis/ChromaprintAnalyzer.h"
#include "core/analysis/AudioAnalysisPipeline.h"
#include "core/analysis/BatchAnalyzer.h"
#include "core/dsp/DSPProcessor.h"
#include "core/dsp/FFTProcessor.h"
#include "core/dsp/FilterProcessor.h"
#include "core/dsp/EQProcessor.h"
#include "core/dsp/CompressorProcessor.h"
#include "core/dsp/LimiterProcessor.h"
#include "core/dsp/GainProcessor.h"
#include "core/dsp/ReverbProcessor.h"
#include "core/dsp/DelayProcessor.h"
#include "core/dsp/FlangerProcessor.h"
#include "core/dsp/PhaserProcessor.h"
#include "core/dsp/ChorusProcessor.h"
#include "core/dsp/VinylSimulator.h"
#include "core/stems/StemSeparator.h"
#include "core/stems/StemCache.h"
#include "core/timestretch/TimeStretchEngine.h"
#include "core/timestretch/SoundTouchWrapper.h"
#include "core/timestretch/PitchShifter.h"
#include "core/effects/EffectsChain.h"
#include "core/effects/TransitionEngine.h"
#include "core/effects/ScratchEngine.h"
#include "core/effects/CrossfadeEngine.h"
#include "core/effects/LoopEngine.h"
#include "core/effects/BeatJumpEngine.h"
#include "core/effects/SendReturnBus.h"
#include "core/mixing/HarmonicMixer.h"
#include "core/mixing/BeatSync.h"
#include "core/mixing/KeyLock.h"
#include "core/mixing/AutoGain.h"
#include "core/mixing/EQMatcher.h"
#include "core/cue/HotCueManager.h"
#include "core/cue/CuePointGenerator.h"
#include "core/automation/AutomationLane.h"
#include "core/automation/AutomationRecorder.h"
#include "core/sampler/SamplerEngine.h"
#include "core/recording/MixRecorder.h"
#include "core/recording/ExportEngine.h"
#include "core/midi/MIDIEngine.h"
#include "core/midi/MIDIController.h"
#include "core/midi/MIDILearn.h"

#include "services/library/TrackDatabase.h"
#include "services/library/TrackDataProvider.h"
#include "services/library/WaveformPrecacheService.h"
#include "services/library/TrackCacheService.h"
#include "services/library/TrackMetadata.h"
#include "services/library/TrackImporter.h"
#include "services/library/TrackScanner.h"
#include "services/library/AutoImport.h"
#include "services/library/CollectionAutoImportService.h"
#include "services/library/SearchEngine.h"
#include "services/library/PlaylistManager.h"
#include "services/library/SmartPlaylist.h"
#include "services/library/DuplicateDetector.h"
#include "services/library/TrackHistory.h"
#include "services/library/TagManager.h"
#include "services/soiree/SoireePhaseService.h"
#include "services/history/SessionHistoryRecorder.h"
#include "services/history/SessionExporter.h"
#include "services/djsoftware/DJSoftwareManager.h"
#include "services/djsoftware/DJSoftwareDetector.h"
#include "services/djsoftware/CollectionSyncService.h"
#include "services/djsoftware/DJImportService.h"
#include "services/djsoftware/SendToDJRouter.h"
#include "services/audio/AudioListenerService.h"
#include "services/djsoftware/UnifiedDJHistory.h"
#include "services/djsoftware/RekordboxHistoryReader.h"
#include "services/djsoftware/rekordbox/RekordboxService.h"
#include "services/djsoftware/virtualdj/VirtualDJService.h"
#include "services/djsoftware/serato/SeratoService.h"
#include "services/djsoftware/traktor/TraktorService.h"
#include "services/djsoftware/enginedj/EngineDJService.h"
#include "services/djsoftware/djaypro/DjayProService.h"
#include "services/streaming/StreamingManager.h"
#include "services/streaming/SpotifyService.h"
#include "services/streaming/AppleMusicService.h"
#include "services/streaming/SoundCloudService.h"
#include "services/streaming/TidalService.h"
#include "services/streaming/YouTubeMusicService.h"
#include "services/streaming/AmazonMusicService.h"
#include "services/streaming/BeatportService.h"
#include "services/streaming/BillboardService.h"
#include "services/suggestions/SuggestionOrchestrator.h"
#include "services/suggestions/MasterSuggestionOrchestrator.h"
#include "services/suggestions/SmartSuggestionEngine.h"
#include "services/suggestions/SmartSuggestEngine.h"
#include "services/suggestions/MyStyleModel.h"
#include "services/suggestions/DJProfileService.h"
#include "services/suggestions/HarmonicSuggestionEngine.h"
#include "services/suggestions/MLSuggestionEngine.h"
#include "services/ai/ONNXInference.h"
#include "services/ai/GenreClassifier.h"
#include "services/ai/MoodRecognizer.h"
#include "services/ai/DJStyleLearner.h"
#include "services/ai/IntelligentCueCreator.h"
#include "services/ai/ClapEmbedQueue.h"
#include "services/preparation/SetPreparation.h"
#include "services/preparation/EventPlanner.h"
#include "services/preparation/SetlistGenerator.h"
#include "services/realtime/RealtimeDetectionManager.h"
#include "services/normalization/LoudnessNormalizer.h"
#include "services/export/MultiFormatExporter.h"
#include "services/security/LicenseService.h"
#include "services/network/HttpClient.h"
#include "services/wordpress/WordPressLicenseClient.h"
#include "services/wordpress/LicenseHeartbeatService.h"

namespace BeatMate {
// WP license bundle (opaque in Application.h) — owns the HttpClient + REST client + 24h timer.
struct Application::WpLicenseBundle
{
    std::shared_ptr<Services::Network::HttpClient> http;
    std::shared_ptr<Services::WordPress::WordPressLicenseClient> client;
    std::unique_ptr<Services::WordPress::LicenseHeartbeatService> heartbeat;
};
} // namespace BeatMate
#include "services/security/LicenseValidator.h"
#include "services/security/EncryptionService.h"
#include "services/security/AntiDebug.h"
#include "services/security/IntegrityCheck.h"
#include "services/security/HardwareId.h"
#include "services/security/ApiKeyManager.h"
#include "services/security/TokenVault.h"
#include "services/config/SettingsManager.h"
#include "services/update/UpdateService.h"
#include "services/config/BackupService.h"
#include "services/config/ThemeManager.h"
#include "services/config/LocalizationService.h"
#include "services/persistence/SettingsStore.h"
#include "ui/styles/ThemeEngine.h"
#include "services/network/HttpClient.h"
#include "services/network/WebSocketClient.h"
#include "services/network/NetworkStatus.h"
#include "services/diagnostics/DiagnosticService.h"
#include "services/diagnostics/SystemInfo.h"
#include "services/diagnostics/PerformanceMonitor.h"
#include "services/plugins/PluginManager.h"
#include "services/plugins/VSTPluginHost.h"

#include <spdlog/spdlog.h>
#include <juce_core/juce_core.h>
#include <cstring>
#include <filesystem>

// Global ServiceLocator pointer(s) for UI access.
BeatMate::ServiceLocator* g_serviceLocator = nullptr;
namespace BeatMate { ServiceLocator* g_serviceLocator = nullptr; }
namespace BeatMate { namespace UI { ServiceLocator* g_serviceLocator = nullptr;
    namespace Widgets { ServiceLocator* g_serviceLocator = nullptr; } } }

static void bindServiceLocator(BeatMate::ServiceLocator* sl)
{
    // Keep both symbols pointing at the same instance.
    ::g_serviceLocator         = sl;
    BeatMate::g_serviceLocator = sl;
    BeatMate::UI::g_serviceLocator = sl;
    BeatMate::UI::Widgets::g_serviceLocator = sl;
}

namespace BeatMate {

static std::atomic<Application*> g_applicationInstance { nullptr };

Application::Application()
    : m_services(std::make_unique<ServiceLocator>()),
      m_backgroundPool(std::make_unique<juce::ThreadPool>(2))
{
    bindServiceLocator(m_services.get());
    g_applicationInstance.store(this, std::memory_order_release);
}

Application::~Application()
{
    // Ensure background jobs are stopped before services are destroyed.
    if (m_backgroundPool)
        m_backgroundPool->removeAllJobs(true /*interrupt*/, 3000 /*ms*/);
    // Clear the global pointers BEFORE m_services is destroyed.
    bindServiceLocator(nullptr);
    g_applicationInstance.store(nullptr, std::memory_order_release);
}

juce::ThreadPool& Application::backgroundPool()
{
    return *m_backgroundPool;
}

juce::ThreadPool* getBackgroundPool()
{
    auto* app = g_applicationInstance.load(std::memory_order_acquire);
    return app ? &app->backgroundPool() : nullptr;
}

bool Application::initialize(ProgressReporter progress)
{
    if (m_initialized) return true;

    auto report = [&progress](float pct, const char* label) {
        if (progress) progress(pct, label);
    };

    try {
        report(0.03f, "Démarrage…");
        setupGlobalExceptionHandlers();

        report(0.06f, "Initialisation des services…");
        spdlog::info("[Init] === Phase 1 START : core services + security");
        registerCoreServices();
        report(0.12f, "Vérification de la licence…");
        initializeSecurity();
        spdlog::info("[Init] === Phase 1 END");

        report(0.20f, "Chargement de la bibliothèque…");
        spdlog::info("[Init] === Phase 2 START : database");
        initializeDatabase();
        spdlog::info("[Init] === Phase 2 END");

        report(0.38f, "Initialisation audio…");
        spdlog::info("[Init] === Phase 3 START : audio engine");
        registerAudioServices();
        initializeAudioEngine();
        spdlog::info("[Init] === Phase 3 END");

        report(0.58f, "Détection des logiciels DJ…");
        spdlog::info("[Init] === Phase 4 START : DJ + streaming + AI + UI services");
        registerDJSoftwareServices();
        report(0.66f, "Connexion aux services de streaming…");
        registerStreamingServices();
        report(0.72f, "Préparation des suggestions…");
        registerAIServices();
        registerUIServices();
        spdlog::info("[Init] === Phase 4 END");

        report(0.80f, "Configuration MIDI…");
        spdlog::info("[Init] === Phase 4.5 START : MIDI wiring (engine -> controller/learn)");
        initializeMIDI();
        spdlog::info("[Init] === Phase 4.5 END");

        report(0.84f, "Synchronisation des collections…");
        spdlog::info("[Init] === Phase 5 START : startCollectionSync (background)");
        startCollectionSync();
        spdlog::info("[Init] === Phase 5 END");

        report(0.88f, "Construction de l'interface…");
        spdlog::info("[Init] === Phase 6 START : MainWindow ctor");
        m_mainWindow = std::make_unique<UI::MainWindow>();
        report(0.97f, "Finalisation…");
        spdlog::info("[Init] === Phase 6 END (MainWindow ready)");

        {
            // Setup wizard désactivé au boot : flag posé directement.
            auto flagFile = juce::File::getSpecialLocation(
                    juce::File::userApplicationDataDirectory)
                .getChildFile("BeatMate").getChildFile("first_run_done");
            if (!flagFile.existsAsFile()) {
                flagFile.getParentDirectory().createDirectory();
                flagFile.replaceWithText("1");
                spdlog::info("[FirstRun] Setup wizard suppressed at boot (auto-flag created).");
            }
        }

        {
            auto* licenseService = m_services->tryGet<Services::Security::LicenseService>();
            if (licenseService)
            {
                auto state = licenseService->getState();
                if (state == Services::Security::LicenseState::TrialExpired)
                {
                    spdlog::warn("[BeatMate] Trial period expired (modal alert disabled — info shown in Settings → Licence instead).");
                }
                else if (state == Services::Security::LicenseState::Trial)
                {
                    int days = licenseService->trialDaysRemaining();
                    spdlog::info("[BeatMate] Trial mode: {} days remaining (modal alert disabled).", days);
                }
                else if (state == Services::Security::LicenseState::Licensed)
                {
                    spdlog::info("[BeatMate] Full license active");
                }
            }
        }

        startLicenseHeartbeat();

        m_initialized = true;
        for (auto& cb : initializedCallbacks_) { if (cb) cb(); }

        spdlog::info("[BeatMate] Application initialized successfully");
        return true;

    } catch (const std::exception& ex) {
        spdlog::critical("[BeatMate] Initialization failed: {}", ex.what());
        return false;
    }
}

void Application::show()
{
    if (m_mainWindow) {
        m_mainWindow->setVisible(true);
        m_mainWindow->toFront(true);
    }
    checkForUpdateAtStartup();
}

void Application::checkForUpdateAtStartup()
{
    bool enabled = true;
    std::string base = "https://beatmate.fr/beatmate-mise-a-jour";
    if (auto* sm = m_services->tryGet<Services::Config::SettingsManager>()) {
        enabled = sm->get<bool>("general.checkUpdates", true);
        const auto v = sm->get<std::string>("general.updateBaseUrl", std::string());
        if (! v.empty()) base = v;
    }
    if (! enabled) return;

    auto svc = std::make_shared<Services::Update::UpdateService>(std::string(BEATMATE_VERSION));
    svc->setBaseUrl(base);

    // Petit délai après l'affichage pour ne pas gêner le boot.
    juce::Timer::callAfterDelay(4000, [svc]()
    {
        svc->checkRemoteAsync([svc](Services::Update::UpdateInfo info)
        {
            if (! info.error.empty() || ! info.available) return;

            juce::String msg;
            msg << juce::String::fromUTF8("Une nouvelle version de BeatMate est disponible : V")
                << juce::String(info.latestVersion)
                << " (version actuelle V" << BEATMATE_VERSION << ").\n\n";
            if (! info.notes.empty()) msg << juce::String::fromUTF8(info.notes.c_str()) << "\n\n";
            msg << juce::String::fromUTF8("Voulez-vous la telecharger et l'installer maintenant ? "
                                          "L'application se fermera pour terminer l'installation. "
                                          "Vous pourrez aussi le faire plus tard depuis Reglages.");

            juce::AlertWindow::showOkCancelBox(
                juce::MessageBoxIconType::QuestionIcon,
                juce::String::fromUTF8("Mise a jour disponible"),
                msg,
                juce::String::fromUTF8("Mettre a jour maintenant"),
                juce::String::fromUTF8("Plus tard"),
                nullptr,
                juce::ModalCallbackFunction::create([svc, info](int result)
                {
                    if (result != 1) return;
                    svc->downloadAndInstallAsync(info.downloadUrl,
                        [](bool ok, std::string /*m*/)
                        {
                            if (ok)
                                if (auto* app = juce::JUCEApplicationBase::getInstance())
                                    app->systemRequestedQuit();
                        });
                }));
        });
    });
}

juce::DocumentWindow* Application::getMainWindow() const
{
    return m_mainWindow.get();
}

void Application::shutdown()
{
    spdlog::info("BeatMate V{} - Shutting down...", BEATMATE_VERSION);
    for (auto& cb : shuttingDownCallbacks_) { if (cb) cb(); }

    if (auto* backup = m_services->tryGet<Services::Config::BackupService>()) {
        backup->createBackup();
    }

    // ORDERING (CRITICAL — was the source of audio-thread use-after-free):
    m_audioCallbackEnabled.store(false, std::memory_order_release);

    if (auto* engine = m_services->tryGet<Core::AudioEngine>()) {
        spdlog::info("Stopping audio engine (detach callback)...");
        engine->stop();
    }

    if (m_wpLicense && m_wpLicense->heartbeat) {
        spdlog::info("Stopping WP license heartbeat...");
        m_wpLicense->heartbeat->stop();
    }

    if (auto* realtime = m_services->tryGet<Services::Realtime::RealtimeDetectionManager>()) {
        spdlog::info("Stopping realtime detection manager...");
        realtime->stop();
    }

    // Stop license heartbeat (local opaque struct; distinct from m_wpLicense)
    stopLicenseHeartbeat();

    if (auto* antiDebug = m_services->tryGet<Services::Security::AntiDebug>()) {
        antiDebug->stop();
    }

    if (auto* sync = m_services->tryGet<Services::DJSoftware::CollectionSyncService>()) {
        spdlog::info("Stopping collection sync service...");
        sync->stop();
    }

    if (auto* clap = m_services->tryGet<Services::AI::ClapEmbedQueue>()) {
        spdlog::info("Stopping CLAP embed queue...");
        clap->stop();
    }

    if (auto* importer = m_services->tryGet<Services::Library::CollectionAutoImportService>()) {
        importer->cancel();
        importer->stopAutoImport();
    }

    // Stop all background jobs BEFORE services so they don't hold dangling refs.
    if (m_backgroundPool) {
        spdlog::info("Stopping background thread pool...");
        m_backgroundPool->removeAllJobs(true /*interrupt*/, 3000 /*ms*/);
    }

    m_mainWindow.reset();

    // Close MIDI input/output cleanly before tearing down the service container.
    shutdownMIDI();

    m_services->shutdownAll();

    spdlog::info("BeatMate V{} - Shutdown complete", BEATMATE_VERSION);
}

ServiceLocator& Application::services() { return *m_services; }
const ServiceLocator& Application::services() const { return *m_services; }

void Application::registerCoreServices()
{
    spdlog::info("[BeatMate] Registering core services...");

    auto dataDir = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory)
        .getChildFile("BeatMate");
    dataDir.createDirectory();

    std::string dataDirStr = dataDir.getFullPathName().toStdString();

    m_services->registerSingleton<Services::Config::SettingsManager>(
        std::make_unique<Services::Config::SettingsManager>());

    m_services->registerSingleton<Services::Config::LocalizationService>(
        std::make_unique<Services::Config::LocalizationService>());

    m_services->registerSingleton<Services::Network::HttpClient>(
        std::make_unique<Services::Network::HttpClient>());
    m_services->registerSingleton<Services::Network::NetworkStatus>(
        std::make_unique<Services::Network::NetworkStatus>());

    m_services->registerSingleton<Services::Diagnostics::DiagnosticService>(
        std::make_unique<Services::Diagnostics::DiagnosticService>());
    m_services->registerSingleton<Services::Diagnostics::SystemInfo>(
        std::make_unique<Services::Diagnostics::SystemInfo>());
    m_services->registerSingleton<Services::Diagnostics::PerformanceMonitor>(
        std::make_unique<Services::Diagnostics::PerformanceMonitor>());

    m_services->registerSingleton<Services::Config::BackupService>(
        std::make_unique<Services::Config::BackupService>());

    {
        auto* sm = m_services->tryGet<Services::Config::SettingsManager>();
        auto* backup = m_services->tryGet<Services::Config::BackupService>();
        if (sm && backup) {
            backup->setMaxBackups(sm->get<int>("backup.maxBackups", 10));
            if (sm->get<bool>("backup.autoBackup", true)) {
                static const int intervals[] = { 60, 1440, 10080 }; // 1=hourly 2=daily 3=weekly
                const int id = juce::jlimit(1, 3, sm->get<int>("backup.backupInterval", 2));
                backup->startAutoBackup(intervals[id - 1]);
                spdlog::info("BackupService: auto-backup armed at boot (interval={}min)", intervals[id - 1]);
            }
        }
    }

    m_services->registerSingleton<Services::Config::ThemeManager>(
        std::make_unique<Services::Config::ThemeManager>());

    m_services->registerSingleton<Services::Plugins::PluginManager>(
        std::make_unique<Services::Plugins::PluginManager>());
    m_services->registerSingleton<Services::Plugins::VSTPluginHost>(
        std::make_unique<Services::Plugins::VSTPluginHost>());

    {
        auto cacheService = std::make_unique<Services::Library::TrackCacheService>();
        Services::Library::TrackCacheConfig cacheConfig;
        cacheConfig.cachePath = dataDirStr + "/cache/analysis";
        cacheConfig.useDiskCache = true;
        cacheConfig.analysisVersion = "1.0";
        cacheService->initialize(cacheConfig);
        m_services->registerSingleton<Services::Library::TrackCacheService>(std::move(cacheService));
    }
}

void Application::registerAudioServices()
{
    spdlog::info("[BeatMate] Registering audio services...");

    m_services->registerSingleton<Core::AudioEngine>(
        std::make_unique<Core::AudioEngine>());
    m_services->registerSingleton<Core::AudioDeviceManager>(
        std::make_unique<Core::AudioDeviceManager>());
    // NOTE: do NOT open the default device here. Core::AudioEngine opens its own
    m_services->registerSingleton<Core::AudioPlayer>(
        std::make_unique<Core::AudioPlayer>());
    m_services->registerSingleton<Core::StreamingPlayer>(
        std::make_unique<Core::StreamingPlayer>());
    m_services->registerSingleton<Core::AudioMixer>(
        std::make_unique<Core::AudioMixer>());
    m_services->registerSingleton<Core::AudioFileReader>(
        std::make_unique<Core::AudioFileReader>());
    m_services->registerSingleton<Core::AudioFileWriter>(
        std::make_unique<Core::AudioFileWriter>());
    m_services->registerSingleton<Core::AudioPreview>(
        std::make_unique<Core::AudioPreview>());

    m_services->registerSingleton<Core::FFTProcessor>(
        std::make_unique<Core::FFTProcessor>());
    m_services->registerSingleton<Core::EQProcessor>(
        std::make_unique<Core::EQProcessor>());

    m_services->registerSingleton<Core::BPMDetector>(
        std::make_unique<Core::BPMDetector>());
    m_services->registerSingleton<Core::KeyDetector>(
        std::make_unique<Core::KeyDetector>());
    m_services->registerSingleton<Core::EnergyAnalyzer>(
        std::make_unique<Core::EnergyAnalyzer>());
    m_services->registerSingleton<Core::LoudnessAnalyzer>(
        std::make_unique<Core::LoudnessAnalyzer>());
    m_services->registerSingleton<Core::StructureDetector>(
        std::make_unique<Core::StructureDetector>());
    m_services->registerSingleton<Core::WaveformGenerator>(
        std::make_unique<Core::WaveformGenerator>());
    m_services->registerSingleton<Core::MultiBandWaveform>(
        std::make_unique<Core::MultiBandWaveform>());
    m_services->registerSingleton<Core::BeatGridGenerator>(
        std::make_unique<Core::BeatGridGenerator>());
    m_services->registerSingleton<Core::SpectrumAnalyzer>(
        std::make_unique<Core::SpectrumAnalyzer>());
    m_services->registerSingleton<Core::AudioAnalysisPipeline>(
        std::make_unique<Core::AudioAnalysisPipeline>());
    m_services->registerSingleton<Core::BatchAnalyzer>(
        std::make_unique<Core::BatchAnalyzer>());

    m_services->registerSingleton<Core::StemSeparator>(
        std::make_unique<Core::StemSeparator>());
    m_services->registerSingleton<Core::StemCache>(
        std::make_unique<Core::StemCache>());

    m_services->registerSingleton<Core::TimeStretchEngine>(
        std::make_unique<Core::TimeStretchEngine>());
    m_services->registerSingleton<Core::PitchShifter>(
        std::make_unique<Core::PitchShifter>());

    m_services->registerSingleton<Core::EffectsChain>(
        std::make_unique<Core::EffectsChain>());
    m_services->registerSingleton<Core::TransitionEngine>(
        std::make_unique<Core::TransitionEngine>());
    m_services->registerSingleton<Core::ScratchEngine>(
        std::make_unique<Core::ScratchEngine>());
    m_services->registerSingleton<Core::CrossfadeEngine>(
        std::make_unique<Core::CrossfadeEngine>());
    m_services->registerSingleton<Core::LoopEngine>(
        std::make_unique<Core::LoopEngine>());
    m_services->registerSingleton<Core::BeatJumpEngine>(
        std::make_unique<Core::BeatJumpEngine>());

    m_services->registerSingleton<Core::HarmonicMixer>(
        std::make_unique<Core::HarmonicMixer>());
    m_services->registerSingleton<Core::BeatSync>(
        std::make_unique<Core::BeatSync>());
    m_services->registerSingleton<Core::AutoGain>(
        std::make_unique<Core::AutoGain>());

    m_services->registerSingleton<Core::HotCueManager>(
        std::make_unique<Core::HotCueManager>());
    m_services->registerSingleton<Core::CuePointGenerator>(
        std::make_unique<Core::CuePointGenerator>());

    m_services->registerSingleton<Core::SamplerEngine>(
        std::make_unique<Core::SamplerEngine>());

    m_services->registerSingleton<Core::AutomationRecorder>(
        std::make_unique<Core::AutomationRecorder>());

    m_services->registerSingleton<Core::MixRecorder>(
        std::make_unique<Core::MixRecorder>());
    m_services->registerSingleton<Core::ExportEngine>(
        std::make_unique<Core::ExportEngine>());

    m_services->registerSingleton<Core::MIDIEngine>(
        std::make_unique<Core::MIDIEngine>());
    m_services->registerSingleton<Core::MIDIController>(
        std::make_unique<Core::MIDIController>());
    m_services->registerSingleton<Core::MIDILearn>(
        std::make_unique<Core::MIDILearn>());

    m_services->registerSingleton<Services::Normalization::LoudnessNormalizer>(
        std::make_unique<Services::Normalization::LoudnessNormalizer>());

    m_services->registerSingleton<Services::Export::MultiFormatExporter>(
        std::make_unique<Services::Export::MultiFormatExporter>());
}

void Application::registerDJSoftwareServices()
{
    spdlog::info("[BeatMate] Registering DJ software services...");

    m_services->registerSingleton<Services::DJSoftware::DJSoftwareDetector>(
        std::make_unique<Services::DJSoftware::DJSoftwareDetector>());

    m_services->registerSingleton<Services::Rekordbox::RekordboxService>(
        std::make_unique<Services::Rekordbox::RekordboxService>());
    m_services->registerSingleton<Services::VirtualDJ::VirtualDJService>(
        std::make_unique<Services::VirtualDJ::VirtualDJService>());
    m_services->registerSingleton<Services::Serato::SeratoService>(
        std::make_unique<Services::Serato::SeratoService>());
    m_services->registerSingleton<Services::Traktor::TraktorService>(
        std::make_unique<Services::Traktor::TraktorService>());
    m_services->registerSingleton<Services::EngineDJ::EngineDJService>(
        std::make_unique<Services::EngineDJ::EngineDJService>());
    m_services->registerSingleton<Services::DjayPro::DjayProService>(
        std::make_unique<Services::DjayPro::DjayProService>());

    m_services->registerSingleton<Services::DJSoftware::DJSoftwareManager>(
        std::make_unique<Services::DJSoftware::DJSoftwareManager>());

    m_services->registerSingleton<Services::DJSoftware::SendToDJRouter>(
        std::make_unique<Services::DJSoftware::SendToDJRouter>());

    m_services->registerSingleton<Services::Audio::AudioListenerService>(
        std::make_unique<Services::Audio::AudioListenerService>());

    // CollectionSyncService - database is already initialized (Phase 2)
    auto& syncDb = m_services->get<Services::Library::TrackDatabase>();
    auto syncDbShared = std::shared_ptr<Services::Library::TrackDatabase>(&syncDb, [](Services::Library::TrackDatabase*){});
    auto& djMgr = m_services->get<Services::DJSoftware::DJSoftwareManager>();
    auto djMgrShared = std::shared_ptr<Services::DJSoftware::DJSoftwareManager>(&djMgr, [](Services::DJSoftware::DJSoftwareManager*){});
    m_services->registerSingleton<Services::DJSoftware::CollectionSyncService>(
        std::make_unique<Services::DJSoftware::CollectionSyncService>(djMgrShared, syncDbShared));

    if (auto* sync = m_services->tryGet<Services::DJSoftware::CollectionSyncService>()) {
        if (auto* pm = m_services->tryGet<Services::Library::PlaylistManager>()) {
            auto pmShared = std::shared_ptr<Services::Library::PlaylistManager>(
                pm, [](Services::Library::PlaylistManager*) {});
            sync->setPlaylistManager(pmShared);
            spdlog::info("[BeatMate] CollectionSyncService wired with PlaylistManager");
        } else {
            spdlog::warn("[BeatMate] PlaylistManager not registered yet — playlist sync disabled");
        }
    }

    m_services->registerSingleton<Services::Realtime::RealtimeDetectionManager>(
        std::make_unique<Services::Realtime::RealtimeDetectionManager>());

    auto& realtimeMgr = m_services->get<Services::Realtime::RealtimeDetectionManager>();
    realtimeMgr.start();

    m_services->registerSingleton<Services::DJSoftware::UnifiedDJHistory>(
        std::make_unique<Services::DJSoftware::UnifiedDJHistory>());

    if (m_backgroundPool) {
        m_backgroundPool->addJob([]() {
            try {
                Services::DJSoftware::RekordboxHistoryReader rb;
                rb.logStartupSummary();
            } catch (const std::exception& e) {
                spdlog::warn("[RB] logStartupSummary failed: {}", e.what());
            } catch (...) {
                spdlog::warn("[RB] logStartupSummary failed (unknown error)");
            }
        });
    }
}

void Application::registerStreamingServices()
{
    spdlog::info("[BeatMate] Registering streaming services...");

    m_services->registerSingleton<Services::Streaming::SpotifyService>(
        std::make_unique<Services::Streaming::SpotifyService>());
    m_services->registerSingleton<Services::Streaming::AppleMusicService>(
        std::make_unique<Services::Streaming::AppleMusicService>());
    m_services->registerSingleton<Services::Streaming::SoundCloudService>(
        std::make_unique<Services::Streaming::SoundCloudService>());
    m_services->registerSingleton<Services::Streaming::TidalService>(
        std::make_unique<Services::Streaming::TidalService>());
    m_services->registerSingleton<Services::Streaming::YouTubeMusicService>(
        std::make_unique<Services::Streaming::YouTubeMusicService>());
    m_services->registerSingleton<Services::Streaming::AmazonMusicService>(
        std::make_unique<Services::Streaming::AmazonMusicService>());
    m_services->registerSingleton<Services::Streaming::BeatportService>(
        std::make_unique<Services::Streaming::BeatportService>());
    m_services->registerSingleton<Services::Streaming::BillboardService>(
        std::make_unique<Services::Streaming::BillboardService>());

    m_services->registerSingleton<Services::Streaming::StreamingManager>(
        std::make_unique<Services::Streaming::StreamingManager>());
}

void Application::registerAIServices()
{
    spdlog::info("[BeatMate] Registering AI services...");

    auto onnxInference = std::make_shared<Services::AI::ONNXInference>();
    m_services->registerSingleton<Services::AI::ONNXInference>(
        std::make_unique<Services::AI::ONNXInference>());

    m_services->registerSingleton<Services::AI::GenreClassifier>(
        std::make_unique<Services::AI::GenreClassifier>(onnxInference));
    m_services->registerSingleton<Services::AI::MoodRecognizer>(
        std::make_unique<Services::AI::MoodRecognizer>(onnxInference));
    m_services->registerSingleton<Services::AI::DJStyleLearner>(
        std::make_unique<Services::AI::DJStyleLearner>());
    m_services->registerSingleton<Services::AI::IntelligentCueCreator>(
        std::make_unique<Services::AI::IntelligentCueCreator>());

    auto& aiDb = m_services->get<Services::Library::TrackDatabase>();
    auto aiDbShared = std::shared_ptr<Services::Library::TrackDatabase>(&aiDb, [](Services::Library::TrackDatabase*){});

    m_services->registerSingleton<Services::Suggestions::SmartSuggestionEngine>(
        std::make_unique<Services::Suggestions::SmartSuggestionEngine>(aiDbShared));
    m_services->registerSingleton<Services::Suggestions::MyStyleModel>(
        std::make_unique<Services::Suggestions::MyStyleModel>(aiDbShared));
    m_services->registerSingleton<Services::Suggestions::SmartSuggestEngine>(
        std::make_unique<Services::Suggestions::SmartSuggestEngine>(aiDbShared));
    if (auto* sync = m_services->tryGet<Services::DJSoftware::CollectionSyncService>()) {
        auto* engine = m_services->tryGet<Services::Suggestions::SmartSuggestEngine>();
        if (engine) {
            sync->setSyncCompletedCallback(
                [engine](int softwareType, bool success) {
                    spdlog::info("[BeatMate] Sync completed (type={}, ok={}) -> "
                                 "invalidating SmartSuggestEngine cache",
                                 softwareType, success);
                    engine->invalidateCache();
                });
        }
    }

    m_services->registerSingleton<Services::AI::ClapEmbedQueue>(
        std::make_unique<Services::AI::ClapEmbedQueue>(aiDbShared));
    if (auto* clap = m_services->tryGet<Services::AI::ClapEmbedQueue>()) {
        if (auto* engine = m_services->tryGet<Services::Suggestions::SmartSuggestEngine>()) {
            engine->setClapLookup([clap] { return clap->snapshot(); });
            clap->setOnPublish([engine](int, int) { engine->invalidateCache(); });
        }
        clap->start();
    }
    m_services->registerSingleton<Services::Suggestions::HarmonicSuggestionEngine>(
        std::make_unique<Services::Suggestions::HarmonicSuggestionEngine>(aiDbShared));
    m_services->registerSingleton<Services::Suggestions::MLSuggestionEngine>(
        std::make_unique<Services::Suggestions::MLSuggestionEngine>(aiDbShared));
    m_services->registerSingleton<Services::Suggestions::SuggestionOrchestrator>(
        std::make_unique<Services::Suggestions::SuggestionOrchestrator>());

    {
        auto master = std::make_unique<Services::Suggestions::MasterSuggestionOrchestrator>(aiDbShared);
        if (auto* my = m_services->tryGet<Services::Suggestions::MyStyleModel>()) {
            master->setMyStyleModel(my);
            master->setMyStylePriorWeight(0.35f);
        }
        m_services->registerSingleton<Services::Suggestions::MasterSuggestionOrchestrator>(std::move(master));
    }

    m_services->registerSingleton<Services::Suggestions::DJProfileService>(
        std::make_unique<Services::Suggestions::DJProfileService>());

    m_services->registerSingleton<Services::Preparation::SetPreparation>(
        std::make_unique<Services::Preparation::SetPreparation>());
    m_services->registerSingleton<Services::Preparation::EventPlanner>(
        std::make_unique<Services::Preparation::EventPlanner>());
    m_services->registerSingleton<Services::Preparation::SetlistGenerator>(
        std::make_unique<Services::Preparation::SetlistGenerator>());
}

void Application::registerUIServices()
{
    spdlog::info("[BeatMate] Registering UI services...");
    // UI services are created lazily by the MainWindow
}

void Application::initializeDatabase()
{
    spdlog::info("[BeatMate] Initializing database...");

    auto dataDir = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory)
        .getChildFile("BeatMate");
    dataDir.createDirectory();

    std::string dbPath = dataDir.getChildFile("beatmate.db").getFullPathName().toStdString();

    m_services->registerSingleton<Services::Library::TrackDatabase>(
        std::make_unique<Services::Library::TrackDatabase>(dbPath));

    auto& db = m_services->get<Services::Library::TrackDatabase>();
    db.initialize();
    db.migrate();

    // This is the ONE AND ONLY shared library DB. Library view, DJ et Studio
    spdlog::info("[BeatMate] Shared library DB = {}", dbPath);

    {
        auto store = std::make_unique<Services::Persistence::SettingsStore>();
        if (!store->initialize(dbPath)) {
            spdlog::error("[BeatMate] SettingsStore init FAILED at {} — "
                          "user settings will not be persisted this session", dbPath);
        }
        m_services->registerSingleton<Services::Persistence::SettingsStore>(std::move(store));
    }

    if (m_backgroundPool) {
        m_backgroundPool->addJob([this]() {
            try { migrateLegacyJsonSettings(); }
            catch (const std::exception& e) {
                spdlog::warn("[SettingsStore] migrateLegacyJsonSettings (bg) failed: {}", e.what());
            } catch (...) {
                spdlog::warn("[SettingsStore] migrateLegacyJsonSettings (bg) failed (unknown)");
            }
        });
    } else {
        migrateLegacyJsonSettings();
    }

    m_services->registerSingleton<Services::Library::TrackDataProvider>(
        std::make_unique<Services::Library::TrackDataProvider>(db));

    {
        auto& providerRef = m_services->get<Services::Library::TrackDataProvider>();
        m_services->registerSingleton<Services::Library::WaveformPrecacheService>(
            std::make_unique<Services::Library::WaveformPrecacheService>(&providerRef));
        m_services->get<Services::Library::WaveformPrecacheService>()
            .startThread(juce::Thread::Priority::low);
    }

    auto dbShared = std::shared_ptr<Services::Library::TrackDatabase>(&db, [](Services::Library::TrackDatabase*){});

    m_services->registerSingleton<Services::Library::TrackMetadata>(
        std::make_unique<Services::Library::TrackMetadata>());
    m_services->registerSingleton<Services::Library::TrackScanner>(
        std::make_unique<Services::Library::TrackScanner>());
    Services::Library::restoreWatchFoldersFromSettings(
        m_services->get<Services::Library::TrackScanner>());
    {
        auto& meta = m_services->get<Services::Library::TrackMetadata>();
        auto metaShared = std::shared_ptr<Services::Library::TrackMetadata>(
            &meta, [](Services::Library::TrackMetadata*) {});
        m_services->registerSingleton<Services::Library::CollectionAutoImportService>(
            std::make_unique<Services::Library::CollectionAutoImportService>(dbShared, metaShared));
    }
    m_services->registerSingleton<Services::Library::SearchEngine>(
        std::make_unique<Services::Library::SearchEngine>(dbShared));
    m_services->registerSingleton<Services::Library::PlaylistManager>(
        std::make_unique<Services::Library::PlaylistManager>(dbShared));

    if (auto* sync = m_services->tryGet<Services::DJSoftware::CollectionSyncService>()) {
        auto& pm = m_services->get<Services::Library::PlaylistManager>();
        auto pmShared = std::shared_ptr<Services::Library::PlaylistManager>(
            &pm, [](Services::Library::PlaylistManager*) {});
        sync->setPlaylistManager(pmShared);
    }

    m_services->registerSingleton<Services::Library::SmartPlaylist>(
        std::make_unique<Services::Library::SmartPlaylist>(dbShared));
    m_services->registerSingleton<Services::Library::DuplicateDetector>(
        std::make_unique<Services::Library::DuplicateDetector>(dbShared));

    {
        auto& metaRef = m_services->get<Services::Library::TrackMetadata>();
        auto metaShared = std::shared_ptr<Services::Library::TrackMetadata>(
            &metaRef, [](Services::Library::TrackMetadata*) {});
        auto importer = std::make_unique<Services::Library::TrackImporter>(dbShared, metaShared);
        auto& dupRef = m_services->get<Services::Library::DuplicateDetector>();
        importer->setDuplicateDetector(std::shared_ptr<Services::Library::DuplicateDetector>(
            &dupRef, [](Services::Library::DuplicateDetector*) {}));
        m_services->registerSingleton<Services::Library::TrackImporter>(std::move(importer));
    }

    if (auto* syncForImport = m_services->tryGet<Services::DJSoftware::CollectionSyncService>()) {
        if (auto* mgrForImport = m_services->tryGet<Services::DJSoftware::DJSoftwareManager>()) {
            auto mgrShared = std::shared_ptr<Services::DJSoftware::DJSoftwareManager>(
                mgrForImport, [](Services::DJSoftware::DJSoftwareManager*) {});
            m_services->registerSingleton<Services::DJSoftware::DJImportService>(
                std::make_unique<Services::DJSoftware::DJImportService>(
                    mgrShared, syncForImport, dbShared));
        }
    }
    m_services->registerSingleton<Services::Library::TrackHistory>(
        std::make_unique<Services::Library::TrackHistory>(dbShared));
    m_services->registerSingleton<Services::Library::TagManager>(
        std::make_unique<Services::Library::TagManager>(dbShared));
    m_services->registerSingleton<Services::Soiree::SoireePhaseService>(
        std::make_unique<Services::Soiree::SoireePhaseService>());
    m_services->registerSingleton<Services::History::SessionHistoryRecorder>(
        std::make_unique<Services::History::SessionHistoryRecorder>(dbShared));

    auto& recRef = m_services->get<Services::History::SessionHistoryRecorder>();
    auto recShared = std::shared_ptr<Services::History::SessionHistoryRecorder>(
        &recRef, [](Services::History::SessionHistoryRecorder*){});
    m_services->registerSingleton<Services::History::SessionExporter>(
        std::make_unique<Services::History::SessionExporter>(recShared, dbShared));

    if (auto* my = m_services->tryGet<Services::Suggestions::MyStyleModel>()) {
        my->trainFromHistoryAsync();
        recRef.setOnPlayRecorded([my](int64_t /*trackId*/) {
            my->notePlay();
        });

        if (auto* unified = m_services->tryGet<Services::DJSoftware::UnifiedDJHistory>()) {
            auto* smart = m_services->tryGet<Services::Suggestions::SmartSuggestEngine>();
            recRef.importExternalHistoryAsync(*unified, [my, smart](int n) {
                if (n <= 0) return;
                spdlog::info("[BeatMate] External DJ history seeded ({} plays) -> "
                             "retraining style models", n);
                my->trainFromHistory();
                if (smart) smart->refreshStyleModel();
            });
        }
    }

    spdlog::info("[BeatMate] Database initialized: {}", dbPath);

    spdlog::info("[BeatMate] Metadata migration at boot: DISABLED (was saturating main thread).");
}

// One-shot JSON -> DB import for legacy files in %APPDATA%/BeatMate/.
void Application::migrateLegacyJsonSettings()
{
    auto* store = m_services->tryGet<Services::Persistence::SettingsStore>();
    if (!store || !store->isOpen()) {
        spdlog::warn("[SettingsStore] migrateLegacyJsonSettings: store not open, skipping");
        return;
    }

    auto appDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                      .getChildFile("BeatMate");

    auto importFile = [&](const char* tag, const char* fname) {
        if (store->isMigrationDone(tag)) return;
        auto f = appDir.getChildFile(fname);
        if (!f.existsAsFile()) {
            // Nothing to import — still mark as done to avoid re-scanning.
            store->markMigrationDone(tag);
            return;
        }
        const auto content = f.loadFileAsString().toStdString();
        if (content.empty()) { store->markMigrationDone(tag); return; }
        bool ok = false;
        if      (std::strcmp(tag, "setlists.current")  == 0) ok = store->upsertSetlist("current", content);
        else if (std::strcmp(tag, "filter_presets")    == 0) ok = store->upsertFilterPreset("__all__", content);
        else if (std::strcmp(tag, "event_plans.checklist") == 0) ok = store->upsertEventPlan("checklist", content);
        else if (std::strcmp(tag, "theme")             == 0) ok = store->setKV("theme.custom.json", content);
        else if (std::strcmp(tag, "shortcuts")         == 0) ok = store->setKV("shortcuts.json",    content);
        else if (std::strcmp(tag, "settings")          == 0) ok = store->setKV("settings.json",     content);
        if (ok) {
            store->markMigrationDone(tag);
            spdlog::info("[SettingsStore] Imported legacy {} ({} bytes) -> DB [{}]",
                         fname, content.size(), tag);
        } else {
            spdlog::warn("[SettingsStore] Failed to import legacy {} — will retry next launch", fname);
        }
    };

    importFile("setlists.current",       "current_set.json");
    importFile("filter_presets",         "filter_presets.json");
    importFile("event_plans.checklist",  "soiree_checklist.json");
    importFile("theme",                  "theme.json");
    importFile("shortcuts",              "shortcuts.json");
    importFile("settings",               "settings.json");
}

void Application::initializeAudioEngine()
{
    spdlog::info("[BeatMate] Initializing audio engine...");

    auto& engine = m_services->get<Core::AudioEngine>();

    // Open the device ONCE at the user's saved sample-rate/buffer.
    double initSR = 44100.0;
    unsigned long initBuf = 256;
    if (auto* settings = m_services->tryGet<Services::Config::SettingsManager>()) {
        static const int    kBuf[] = { 64, 128, 256, 512, 1024, 2048, 4096 };
        static const double kSR[]  = { 44100.0, 48000.0, 88200.0, 96000.0 };
        initBuf = static_cast<unsigned long>(kBuf[juce::jlimit(0, 6, settings->get<int>("audio.bufferSize", 3) - 1)]);
        initSR  = kSR[juce::jlimit(0, 3, settings->get<int>("audio.sampleRate", 1) - 1)];
    }
    if (!engine.initialize(initSR, initBuf)) {
        spdlog::error("[BeatMate] Failed to initialize audio engine");
    } else {
        spdlog::info("[BeatMate] Audio engine initialized (latency: {:.1f}ms)",
            engine.latencyMs());
    }

    auto* streamerPtr = m_services->tryGet<Core::StreamingPlayer>();

    auto* playerPtr = m_services->tryGet<Core::AudioPlayer>();
    auto* previewPtr = m_services->tryGet<Core::AudioPreview>();
    if (playerPtr) {
        // Mark callback live BEFORE wiring it, so the very first invocation is allowed.
        m_audioCallbackEnabled.store(true, std::memory_order_release);
        std::atomic<bool>* enabledPtr = &m_audioCallbackEnabled;
        engine.setCallback([playerPtr, streamerPtr, previewPtr, enabledPtr](float* output, const float* /*input*/, unsigned long frames, int channels) {
            if (output == nullptr || frames == 0 || channels <= 0) return;
            // Shutdown gate: shutdown() flipped the flag → output silence.
            if (!enabledPtr->load(std::memory_order_acquire)) {
                std::memset(output, 0, frames * channels * sizeof(float));
                return;
            }
            try {
                if (previewPtr && previewPtr->isPlaying()) {
                    // Pré-écoute (éditeur audio, aperçus) : prioritaire.
                    previewPtr->processBlock(output, static_cast<int>(frames), channels);
                } else if (streamerPtr && streamerPtr->isPlaying()) {
                    streamerPtr->processBlock(output, static_cast<int>(frames), channels);
                } else if (playerPtr) {
                    playerPtr->processBlock(output, static_cast<int>(frames), channels);
                }
            } catch (...) {
                // Audio thread must never propagate an exception to JUCE.
                std::memset(output, 0, frames * channels * sizeof(float));
            }
        });
        spdlog::info("[BeatMate] AudioPlayer + StreamingPlayer connected to AudioEngine callback");

        auto* licenseService = m_services->tryGet<Services::Security::LicenseService>();
        if (licenseService) {
            double maxSec = licenseService->getMaxPlaybackSeconds();
            spdlog::info("[BeatMate] LicenseService state: maxSec={:.0f}, canUse={}",
                         maxSec, licenseService->canUseApp() ? "true" : "false");
            if (maxSec > 0.0) {
                playerPtr->setMaxPlaybackSeconds(maxSec);
                spdlog::info("[BeatMate] Trial mode ACTIVE: playback limited to {:.0f}s TOTAL", maxSec);
            } else {
                spdlog::info("[BeatMate] Full license: unlimited playback");
            }
        } else {
            playerPtr->setMaxPlaybackSeconds(300.0);
            spdlog::warn("[BeatMate] No LicenseService found - forcing 300s trial limit");
        }
    } else {
        spdlog::error("[BeatMate] AudioPlayer not available for audio callback!");
    }

    if (!engine.start()) {
        spdlog::error("[BeatMate] Failed to start audio engine");
    }

    // Prepare StreamingPlayer AFTER engine start (sample rate now known)
    if (streamerPtr) {
        double sr = engine.getSampleRate();
        int bs = static_cast<int>(engine.getBufferSize());
        if (sr <= 0) sr = 44100.0;
        if (bs <= 0) bs = 512;
        streamerPtr->prepare(sr, bs);
    }

}

void Application::initializeMIDI()
{
    auto* midiEngine     = m_services->tryGet<Core::MIDIEngine>();
    auto* midiController = m_services->tryGet<Core::MIDIController>();
    auto* midiLearn      = m_services->tryGet<Core::MIDILearn>();

    if (!midiEngine) {
        spdlog::warn("[MIDI] MIDIEngine not registered — skipping wiring");
        return;
    }

    try {
        if (!midiEngine->isInitialized() && !midiEngine->initialize()) {
            spdlog::warn("[MIDI] initialize() returned false — module disabled");
            return;
        }

        // Bind callbacks BEFORE opening the device so we don't miss the first
        midiEngine->setNoteOnCallback(
            [midiController, midiLearn](int channel, int note, int velocity) {
                try {
                    if (midiLearn && midiLearn->isLearning()) {
                        midiLearn->onNote(channel, note, velocity);
                        return;
                    }
                    if (midiController) {
                        auto action = midiController->processNote(note, velocity);
                        if (!action.empty()) {
                            spdlog::debug("[MIDI] Note ch={} n={} v={} -> {}",
                                          channel, note, velocity, action);
                        }
                    }
                } catch (...) { /* never throw on MIDI thread */ }
            });

        midiEngine->setNoteOffCallback(
            [](int /*channel*/, int /*note*/, int /*velocity*/) {
                // No per-note-off handler currently.
            });

        midiEngine->setCCCallback(
            [midiController, midiLearn](int channel, int cc, int value) {
                try {
                    if (midiLearn && midiLearn->isLearning()) {
                        midiLearn->onCC(channel, cc, value);
                        return;
                    }
                    if (midiController) {
                        auto action = midiController->processCC(cc, value);
                        if (!action.empty()) {
                            spdlog::debug("[MIDI] CC ch={} cc={} v={} -> {}",
                                          channel, cc, value, action);
                        }
                    }
                } catch (...) { /* never throw on MIDI thread */ }
            });

        auto* settings = m_services->tryGet<Services::Config::SettingsManager>();
        std::string preferred;
        if (settings) {
            preferred = settings->get<std::string>("midi.preferredDeviceId", std::string{});
        }

        // Device enumeration + openDevice can take 100ms-1s on Windows → async.
        auto openJob = [midiEngine, preferred]() {
            try {
                auto devices = midiEngine->getInputDevices();
                if (devices.empty()) {
                    spdlog::info("[MIDI] No input devices detected");
                    return;
                }

                int chosen = -1;
                std::string chosenName;
                if (!preferred.empty()) {
                    for (const auto& d : devices) {
                        if (d.name == preferred) { chosen = d.id; chosenName = d.name; break; }
                    }
                    if (chosen < 0) {
                        spdlog::info("[MIDI] Preferred device '{}' not found, falling back to first available", preferred);
                    }
                }
                if (chosen < 0) {
                    chosen = devices.front().id;
                    chosenName = devices.front().name;
                }

                if (midiEngine->openInput(chosen)) {
                    spdlog::info("[MIDI] opened device {} (id={})", chosenName, chosen);
                } else {
                    spdlog::warn("[MIDI] failed to open device {} (id={})", chosenName, chosen);
                }
            } catch (const std::exception& e) {
                spdlog::warn("[MIDI] async open failed: {}", e.what());
            } catch (...) {
                spdlog::warn("[MIDI] async open failed (unknown)");
            }
        };

        if (m_backgroundPool) {
            m_backgroundPool->addJob(openJob);
        } else {
            // Fallback: pool unavailable, open inline (still safe, just blocking).
            openJob();
        }
    } catch (const std::exception& e) {
        spdlog::warn("[MIDI] wiring failed: {}", e.what());
    } catch (...) {
        spdlog::warn("[MIDI] wiring failed (unknown error)");
    }
}

void Application::shutdownMIDI()
{
    try {
        if (auto* midiEngine = m_services->tryGet<Core::MIDIEngine>()) {
            midiEngine->closeInput();
            midiEngine->closeOutput();
            spdlog::info("[MIDI] input/output closed");
        }
    } catch (...) { /* swallow on shutdown path */ }
}

void Application::initializeSecurity()
{
    spdlog::info("[BeatMate] Initializing security...");

    m_services->registerSingleton<Services::Security::HardwareId>(
        std::make_unique<Services::Security::HardwareId>());
    m_services->registerSingleton<Services::Security::EncryptionService>(
        std::make_unique<Services::Security::EncryptionService>());
    m_services->registerSingleton<Services::Security::ApiKeyManager>(
        std::make_unique<Services::Security::ApiKeyManager>());
    m_services->registerSingleton<Services::Security::TokenVault>(
        std::make_unique<Services::Security::TokenVault>());
    m_services->registerSingleton<Services::Security::AntiDebug>(
        std::make_unique<Services::Security::AntiDebug>());
    m_services->registerSingleton<Services::Security::IntegrityCheck>(
        std::make_unique<Services::Security::IntegrityCheck>());

    m_services->registerSingleton<Services::Security::LicenseValidator>(
        std::make_unique<Services::Security::LicenseValidator>());
    m_services->registerSingleton<Services::Security::LicenseService>(
        std::make_unique<Services::Security::LicenseService>());

    {
        auto* settings = m_services->tryGet<Services::Config::SettingsManager>();
        std::string wpUrl    = settings ? settings->get<std::string>("wp_base_url", "") : "";
        std::string wpApiKey = settings ? settings->get<std::string>("wp_api_key",  "") : "";

        auto http   = std::make_shared<Services::Network::HttpClient>();
        auto client = std::make_shared<Services::WordPress::WordPressLicenseClient>(
            http, wpUrl, wpApiKey);

        auto& licenseService = m_services->get<Services::Security::LicenseService>();
        auto heartbeat = std::make_unique<Services::WordPress::LicenseHeartbeatService>(
            client, licenseService);

        // Hook into SettingsManager so that changes to URL/key are picked up live.
        if (settings) {
            settings->onChange("wp_base_url", [settings, client]() {
                client->setBaseUrl(settings->get<std::string>("wp_base_url", ""));
            });
            settings->onChange("wp_api_key", [settings, client]() {
                client->setApiKey(settings->get<std::string>("wp_api_key", ""));
            });
        }

        // Start heartbeat regardless of activation state — runOnce() short-circuits
        if (!wpUrl.empty()) {
            heartbeat->start(/*firstDelayMs*/ 5 * 60 * 1000,
                             /*periodMs   */ 24 * 60 * 60 * 1000,
                             /*offlineCap */ 7);
            spdlog::info("[BeatMate] WP license heartbeat scheduled (URL={})", wpUrl);
        } else {
            spdlog::info("[BeatMate] WP license heartbeat NOT scheduled — wp_base_url is empty");
        }

        m_wpLicense = std::make_unique<WpLicenseBundle>(WpLicenseBundle{
            std::move(http), std::move(client), std::move(heartbeat)
        });
    }

    auto& antiDebug = m_services->get<Services::Security::AntiDebug>();
    antiDebug.start();
    spdlog::info("[BeatMate] Anti-Debug service started");

    auto& integrity = m_services->get<Services::Security::IntegrityCheck>();
    auto exePath = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName().toStdString();
    integrity.storeStartupHash(exePath); // cheap: just remembers the path

    if (m_backgroundPool) {
        m_backgroundPool->addJob([this, &integrity, exePath]() {
            try {
                auto hash = integrity.computeHash(exePath);
                if (!hash.empty()) {
                    {
                        std::lock_guard<std::mutex> lk(m_integrityHashMutex);
                        m_integrityHash = hash;
                    }
                    m_integrityHashReady.store(true, std::memory_order_release);
                    spdlog::info("[BeatMate] Integrity check passed (hash: {}...)",
                                 hash.substr(0, 16));
                    if (integrity.checkCriticalSections()) {
                        spdlog::info("[BeatMate] Critical section check passed");
                    } else {
                        // WARN, not FATAL — boot must not be blocked.
                        spdlog::warn("[BeatMate] Critical section check FAILED - binary may be patched");
                    }
                } else {
                    spdlog::warn("[BeatMate] Integrity check: could not compute hash (background)");
                }
            } catch (const std::exception& e) {
                spdlog::warn("[BeatMate] Integrity check threw: {}", e.what());
            } catch (...) {
                spdlog::warn("[BeatMate] Integrity check threw (unknown)");
            }
        });
    } else {
        // Pool unavailable: fall back to inline.
        auto hash = integrity.computeHash(exePath);
        if (!hash.empty()) {
            {
                std::lock_guard<std::mutex> lk(m_integrityHashMutex);
                m_integrityHash = hash;
            }
            m_integrityHashReady.store(true, std::memory_order_release);
            spdlog::info("[BeatMate] Integrity check passed (hash: {}...)", hash.substr(0, 16));
            if (!integrity.checkCriticalSections()) {
                spdlog::warn("[BeatMate] Critical section check FAILED - binary may be patched");
            }
        } else {
            spdlog::warn("[BeatMate] Integrity check: could not compute hash");
        }
    }
}

void Application::startCollectionSync()
{
    spdlog::info("[BeatMate] Starting collection sync...");

    auto& sync = m_services->get<Services::DJSoftware::CollectionSyncService>();

    spdlog::info("[BeatMate] Running initial DJ software collection sync (background)...");
    auto* syncPtr = &sync;
    m_backgroundPool->addJob([syncPtr]() {
        try {
            syncPtr->syncAll();
            spdlog::info("[BeatMate] Initial sync completed");
        } catch (const std::exception& e) {
            spdlog::error("[BeatMate] Initial sync failed: {}", e.what());
        } catch (...) {
            spdlog::error("[BeatMate] Initial sync failed (unknown error)");
        }
    });

    sync.start(30);

    // One backlog sweep per boot over watched folders.
    m_backgroundPool->addJob([this]() {
        auto* scanner  = m_services->tryGet<Services::Library::TrackScanner>();
        auto* importer = m_services->tryGet<Services::Library::CollectionAutoImportService>();
        if (!scanner || !importer) return;
        int imported = 0;
        for (const auto& folder : scanner->getWatchedFolders()) {
            try {
                auto summary = importer->importDirectory(folder, true);
                imported += summary.filesImported;
                spdlog::info("[AutoImport] Backlog sweep '{}': {} importes, {} deja connus, {} echecs",
                             folder, summary.filesImported, summary.filesSkipped, summary.filesFailed);
            } catch (const std::exception& e) {
                spdlog::error("[AutoImport] Backlog sweep '{}' failed: {}", folder, e.what());
            }
        }
        if (imported > 0) {
            if (auto* engine = m_services->tryGet<Services::Suggestions::SmartSuggestEngine>())
                engine->invalidateCache();
            if (auto* clap = m_services->tryGet<Services::AI::ClapEmbedQueue>())
                clap->rescanLibrary();
        }
    });
}

// Dump minimal crash info to %APPDATA%/BeatMate/crashes/.
static void writeCrashDump(void* crashContext)
{
    (void)crashContext;

    auto crashDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                        .getChildFile("BeatMate").getChildFile("crashes");
    crashDir.createDirectory();
    auto now = juce::Time::getCurrentTime();
    auto filename = juce::String("crash_") + now.formatted("%Y%m%d_%H%M%S") + ".log";
    auto file = crashDir.getChildFile(filename);

    juce::String contents;
    contents << "BeatMate crash report\n";
    contents << "Timestamp : " << now.toISO8601(true) << "\n";
    contents << "Version   : " << BEATMATE_VERSION << "\n";
    contents << "OS        : " << juce::SystemStats::getOperatingSystemName() << "\n";
    contents << "CPU       : " << juce::SystemStats::getCpuVendor() << " "
             << juce::SystemStats::getCpuModel() << "\n";
    contents << "Memory MB : " << juce::SystemStats::getMemorySizeInMegabytes() << "\n";
    contents << "Stack     :\n" << juce::SystemStats::getStackBacktrace() << "\n";

    file.replaceWithText(contents, false, false);
}

void Application::setupGlobalExceptionHandlers()
{
    juce::SystemStats::setApplicationCrashHandler(&writeCrashDump);

    std::set_terminate([]() {
        spdlog::critical("[BeatMate] Unhandled exception - terminating");
        // Trigger the JUCE crash handler path too so we get a dump on std::terminate.
        try { writeCrashDump(nullptr); } catch (...) {}
        spdlog::default_logger()->flush();
        std::abort();
    });

    spdlog::info("[BeatMate] Crash handler installed → %APPDATA%/BeatMate/crashes/");
}


// Heartbeat implementation (opaque in header)
struct Application::HeartbeatImpl : public juce::Timer
{
    explicit HeartbeatImpl(ServiceLocator* services) : services_(services) {}

    void timerCallback() override
    {
        if (!services_) return;
        auto* licenseService = services_->tryGet<Services::Security::LicenseService>();
        if (!licenseService) return;

        bool ok = licenseService->verifyIntegrity();
        if (!ok) {
            spdlog::warn("[BeatMate] License heartbeat: integrity check FAILED");
            auto* integrity = services_->tryGet<Services::Security::IntegrityCheck>();
            if (integrity && !integrity->checkCriticalSections())
                spdlog::error("[BeatMate] Binary tampering detected!");
        }
    }

    ServiceLocator* services_ = nullptr;
};

void Application::startLicenseHeartbeat()
{
    spdlog::info("[BeatMate] Starting license integrity heartbeat (60s interval)");
    m_licenseHeartbeat = std::make_unique<HeartbeatImpl>(m_services.get());
    m_licenseHeartbeat->startTimer(60000);
}

void Application::stopLicenseHeartbeat()
{
    if (m_licenseHeartbeat) {
        m_licenseHeartbeat->stopTimer();
        m_licenseHeartbeat.reset();
        spdlog::info("[BeatMate] License heartbeat stopped");
    }
}

} // namespace BeatMate
