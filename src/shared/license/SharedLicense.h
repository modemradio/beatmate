#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "services/security/LicenseService.h"

namespace BeatMate::Services::Network { class HttpClient; }
namespace BeatMate::Services::WordPress { class WordPressLicenseClient; class LicenseHeartbeatService; }

namespace BeatMate::Shared {

struct WpCredentials {
    std::string baseUrl;
    std::string apiKey;
    bool isConfigured() const { return ! baseUrl.empty() && ! apiKey.empty(); }
};

struct ActivationOutcome {
    bool        success = false;
    std::string message;
};

class SharedLicense {
public:
    static SharedLicense& instance();

    Services::Security::LicenseState state() const;
    bool canRunApp() const;
    bool isFullLicense() const;
    bool isTrial() const;
    bool isTrialExpired() const;
    int  trialDaysRemaining() const;
    double maxPlaybackSeconds() const;

    bool isFeatureAvailable(const std::string& feature) const;
    bool canUseStems()  const { return isFeatureAvailable("stems"); }
    bool canUseExport() const { return isFeatureAvailable("export"); }
    bool canUseStudio() const { return isFeatureAvailable("studio"); }
    bool canUseMix()    const { return isFeatureAvailable("studio"); }
    bool canUseJingle() const { return isFeatureAvailable("jingle"); }
    bool canUseLive()   const { return isFeatureAvailable("live"); }
    bool canUsePerfDJ() const { return isFeatureAvailable("perfdj"); }

    std::string licenseType() const;
    std::string licenseKeyMasked() const;
    int64_t     expiresAtEpoch() const;

    WpCredentials credentials() const;
    bool wpConfigured() const { return credentials().isConfigured(); }
    void saveCredentials(const WpCredentials& creds);

    void activate(const std::string& key,
                  const std::string& email,
                  const std::string& prenom,
                  const std::string& nom,
                  std::function<void(ActivationOutcome)> cb);

    void deactivate(std::function<void(ActivationOutcome)> cb);
    void removeLocal();

    void reload();
    void startHeartbeat();
    void shutdown();

private:
    SharedLicense();
    ~SharedLicense();
    SharedLicense(const SharedLicense&) = delete;
    SharedLicense& operator=(const SharedLicense&) = delete;

    static std::string configFilePath();
    WpCredentials loadCredentials() const;
    void rebuildClient();
    void startHeartbeatLocked();

    mutable std::mutex mutex_;
    std::unique_ptr<Services::Security::LicenseService> license_;
    std::shared_ptr<Services::Network::HttpClient> http_;
    std::shared_ptr<Services::WordPress::WordPressLicenseClient> wpClient_;
    std::unique_ptr<Services::WordPress::LicenseHeartbeatService> heartbeat_;
    WpCredentials creds_;
    std::atomic<bool> shuttingDown_ { false };
};

}
