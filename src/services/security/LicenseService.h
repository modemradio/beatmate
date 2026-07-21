#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
namespace BeatMate::Services::Security {


enum class LicenseTier {
    Trial,          // Free 7-day trial
    Personal,       // Basic - no Live/PerfDJ
    Professional,   // Pro - no Live/PerfDJ
    Premium         // Everything unlocked
};

enum class LicenseBilling {
    None,           // Trial
    Monthly,        // Recurring monthly
    Annual          // Recurring annual
};

struct LicenseInfo {
    std::string key;
    std::string type;       // "Personal", "Professional", "Premium"
    LicenseTier tier = LicenseTier::Trial;
    LicenseBilling billing = LicenseBilling::None;
    bool isValid = false;
    int64_t expiresAt = 0;
    int maxMachines = 1;
    std::vector<std::string> features;

    std::string signature;

    std::string email;
};

enum class LicenseState {
    Licensed,       // Full license active
    Trial,          // Within 7-day trial period
    TrialExpired,   // Trial period over, no license
    Unlicensed      // No trial started
};

class LicenseService {
public:
    LicenseService();

    bool activate(const std::string& key);

    void activateFromServer(const std::string& key,
                            const std::string& type,
                            int64_t expiresAtEpoch,
                            const std::string& signature,
                            const std::string& email);

    bool deactivate();
    LicenseInfo checkLicense() const;
    bool isFeatureAvailable(const std::string& feature) const;
    bool isActivated() const { return activated_; }

    const std::string& getKey()       const { return license_.key; }
    const std::string& getSignature() const { return license_.signature; }
    const std::string& getEmail()     const { return license_.email; }

    void forceLocalRevocation(const std::string& reason);

    LicenseState getState() const;
    bool isTrial() const { return getState() == LicenseState::Trial; }
    bool isTrialExpired() const { return getState() == LicenseState::TrialExpired; }
    bool isFullLicense() const { return getState() == LicenseState::Licensed; }
    bool canUseApp() const;
    int trialDaysRemaining() const;
    int64_t getTrialStartTimestamp() const { return trialStartTimestamp_; }

    double getMaxPlaybackSeconds() const;

    bool verifyIntegrity();

    using IntegrityFailCallback = std::function<void()>;
    void setIntegrityFailCallback(IntegrityFailCallback cb) { integrityFailCallback_ = std::move(cb); }

    bool canUseStudio() const { return isFeatureAvailable("studio"); }
    bool canUseJingle() const { return isFeatureAvailable("jingle"); }
    bool canUseMix() const { return isFeatureAvailable("studio"); }
    bool canUseLive() const { return isFeatureAvailable("live"); }
    bool canUsePerfDJ() const { return isFeatureAvailable("perfdj"); }

    bool canUseAgenda() const { return isFeatureAvailable("agenda"); }
    bool canUseStems() const { return isFeatureAvailable("stems"); }
    bool canUseExport() const { return isFeatureAvailable("export"); }
    bool canUseExportDJ() const { return isFeatureAvailable("export"); }

    static constexpr int kTrialDays = 7;
    static constexpr double kTrialMaxPlaybackSeconds = 300.0; // 5 minutes

private:
    void initTrial();
    void loadTrialState();
    void saveTrialState();
    void saveLicenseToDisk();
    void loadLicenseFromDisk();
    std::string getTrialFilePath() const;
    std::string getLicenseFilePath() const;

    bool verifyDiskConsistency() const;

    mutable LicenseInfo license_;
    mutable bool activated_ = false;
    int64_t trialStartTimestamp_ = 0;
    IntegrityFailCallback integrityFailCallback_;
    mutable std::mutex stateMutex_;
};

} // namespace BeatMate::Services::Security
