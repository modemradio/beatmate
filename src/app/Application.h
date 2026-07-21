#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <functional>
#include <string>
#include <vector>
#include <juce_core/juce_core.h>

namespace juce { class ThreadPool; class DocumentWindow; }

namespace BeatMate {

class ServiceLocator;

namespace UI { class MainWindow; }

namespace Services {
    class SettingsManager;
    class TrackDatabase;
    class CollectionSyncService;
    class LicenseService;
    class DiagnosticService;
    class BackupService;
    class LocalizationService;
}

namespace Core {
    class AudioEngine;
    class AudioAnalysisPipeline;
}

class Application
{
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    using ProgressReporter = std::function<void(float, const char*)>;
    bool initialize(ProgressReporter progress = {});
    void show();
    void shutdown();

    juce::DocumentWindow* getMainWindow() const;

    ServiceLocator& services();
    const ServiceLocator& services() const;

    // Owned by Application: shut down before services are destroyed.
    juce::ThreadPool& backgroundPool();

    using Callback = std::function<void()>;
    void onInitialized(Callback cb) { initializedCallbacks_.push_back(std::move(cb)); }
    void onShuttingDown(Callback cb) { shuttingDownCallbacks_.push_back(std::move(cb)); }

private:
    void registerCoreServices();
    void registerAudioServices();
    void registerDJSoftwareServices();
    void registerStreamingServices();
    void registerAIServices();
    void registerUIServices();
    void initializeDatabase();
    void initializeAudioEngine();
    void initializeSecurity();
    void initializeMIDI();
    void shutdownMIDI();
    void startCollectionSync();
    void checkForUpdateAtStartup();
    void migrateLegacyJsonSettings();
    void setupGlobalExceptionHandlers();
    void startLicenseHeartbeat();
    void stopLicenseHeartbeat();

    std::unique_ptr<ServiceLocator> m_services;
    std::unique_ptr<UI::MainWindow> m_mainWindow;
    std::unique_ptr<juce::ThreadPool> m_backgroundPool;
    bool m_initialized = false;

    std::atomic<bool> m_audioCallbackEnabled { false };

    std::atomic<bool> m_integrityHashReady { false };
    std::string       m_integrityHash;
    std::mutex        m_integrityHashMutex;  // protects m_integrityHash write

    std::vector<Callback> initializedCallbacks_;
    std::vector<Callback> shuttingDownCallbacks_;

    struct HeartbeatImpl;
    std::unique_ptr<HeartbeatImpl> m_licenseHeartbeat;

    struct WpLicenseBundle;
    std::unique_ptr<WpLicenseBundle> m_wpLicense;
};

// Returns nullptr if Application has not been constructed yet.
juce::ThreadPool* getBackgroundPool();

} // namespace BeatMate
