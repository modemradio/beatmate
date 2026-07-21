#pragma once
#include <juce_events/juce_events.h>
#include <memory>
#include <atomic>

namespace BeatMate::Services::Security { class LicenseService; }

namespace BeatMate::Services::WordPress {

class WordPressLicenseClient;

/// Periodic license verification timer.
class LicenseHeartbeatService : public juce::Timer {
public:
    LicenseHeartbeatService(std::shared_ptr<WordPressLicenseClient> client,
                            BeatMate::Services::Security::LicenseService& licenseService);

    /// Start the timer. firstDelayMs runs after this delay; subsequent calls every periodMs.
    void start(int firstDelayMs = 5 * 60 * 1000,
               int periodMs    = 24 * 60 * 60 * 1000,
               int maxConsecutiveOfflineFailures = 7);

    void stop();
    bool isRunning() const { return running_; }

    // Public for unit-style triggering / debugging.
    void runOnce();

private:
    void timerCallback() override;

    // Persisted offline-failure counter — survives reboot to prevent the user
    void loadPersistedState();
    void persistState() const;

    std::shared_ptr<WordPressLicenseClient> client_;
    BeatMate::Services::Security::LicenseService& licenseService_;
    std::atomic<bool> running_{false};
    std::atomic<int>  consecutiveFailures_{0};
    int periodMs_ = 24 * 60 * 60 * 1000;
    int maxOfflineFailures_ = 7;
    bool firstTickDone_ = false;
};

} // namespace BeatMate::Services::WordPress
