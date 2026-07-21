#pragma once
#include <string>
#include <functional>
#include <memory>

namespace BeatMate::Services::Network { class HttpClient; }
namespace BeatMate::Services::Security { class LicenseService; class HardwareId; }

namespace BeatMate::Services::WordPress {

struct ActivationResult {
    bool        success      = false;
    int         httpStatus   = 0;
    std::string type;          // "Personal" | "Professional" | "Premium"
    int64_t     expiresAtEpoch= 0;
    std::string signature;     // 64 hex chars HMAC-SHA256
    std::string error;         // user-facing error if !success
};

struct DeactivationResult {
    bool        success    = false;
    int         httpStatus = 0;
    std::string error;
};

enum class HeartbeatAction {
    None,        // license still valid, do nothing
    Revoke,      // server says revoke immediately
    Expire,      // license expired (server-side)
    Deactivate,  // HWID mismatch / signature mismatch / key not found
    NetworkFail  // could not reach the server (do NOT revoke unless tolerance exceeded)
};

struct HeartbeatResult {
    HeartbeatAction action     = HeartbeatAction::None;
    bool            valid      = true;
    int             httpStatus = 0;
    std::string     message;
};

/// Async REST client for /wp-json/beatmate/v1/* endpoints.
class WordPressLicenseClient {
public:
    WordPressLicenseClient(std::shared_ptr<Network::HttpClient> http,
                           std::string baseUrl,
                           std::string apiKey);

    void setBaseUrl(std::string url) { baseUrl_ = std::move(url); }
    void setApiKey(std::string key)  { apiKey_  = std::move(key); }
    void setAppVersion(std::string v){ appVersion_ = std::move(v); }

    // Posts to /activate with hwid+mac auto-derived from HardwareId.
    void activate(const std::string& key,
                  const std::string& email,
                  const std::string& prenom,
                  const std::string& nom,
                  const std::string& machineName,
                  std::function<void(ActivationResult)> cb);

    // Posts to /deactivate with the currently-stored key+hwid.
    void deactivate(const std::string& key,
                    std::function<void(DeactivationResult)> cb);

    // Posts to /heartbeat. Pass the server signature received at activation.
    void heartbeat(const std::string& key,
                   const std::string& signature,
                   std::function<void(HeartbeatResult)> cb);

private:
    std::shared_ptr<Network::HttpClient> http_;
    std::string baseUrl_;
    std::string apiKey_;
    std::string appVersion_ = "12.0.0";

    std::string endpoint(const char* path) const;
};

} // namespace BeatMate::Services::WordPress
