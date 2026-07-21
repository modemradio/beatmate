#pragma once
#include <cstdint>

#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "../../models/StreamingTrack.h"
#include "../../models/Playlist.h"

namespace BeatMate::Services::Streaming {

struct HttpResponse {
    int status = 0;
    std::string body;
    bool transportOk = false;
    bool ok() const { return transportOk && (status == 0 || (status >= 200 && status < 300)); }
};

struct OAuthToken {
    std::string accessToken;
    std::string refreshToken;
    std::string tokenType = "Bearer";
    int64_t expiresAt = 0;
    std::string scope;

    bool isExpired() const {
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return now >= expiresAt;
    }
};

struct StreamingSearchResult {
    std::vector<Models::StreamingTrack> tracks;
    int totalResults = 0;
    int offset = 0;
    int limit = 20;
};

class StreamingServiceBase {
public:
    StreamingServiceBase(const std::string& serviceName, Models::StreamingServiceType type);
    virtual ~StreamingServiceBase() = default;

    virtual bool authenticate(const std::string& clientId, const std::string& redirectUri,
                              const std::vector<std::string>& scopes) = 0;
    virtual bool refreshAccessToken() = 0;
    virtual void logout();
    bool isAuthenticated() const;

    virtual StreamingSearchResult search(const std::string& query, int limit = 20, int offset = 0) = 0;
    virtual std::optional<Models::StreamingTrack> getTrack(const std::string& trackId) = 0;
    virtual std::vector<Models::Playlist> getPlaylists() = 0;

    std::string getServiceName() const { return serviceName_; }
    Models::StreamingServiceType getServiceType() const { return serviceType_; }

    bool canMakeRequest();
    void setRateLimit(int requestsPerSecond) { maxRequestsPerSecond_ = requestsPerSecond; }

    OAuthToken getToken() const;

    static constexpr int kHttpTimeoutMs = 8000;

    static HttpResponse httpGet(const std::string& endpoint,
                                const juce::String& extraHeaders,
                                int timeoutMs = kHttpTimeoutMs,
                                int softRetries = 1);
    static HttpResponse httpSend(const std::string& endpoint,
                                 const juce::String& verb,
                                 const std::string& body,
                                 const juce::String& extraHeaders,
                                 int timeoutMs = kHttpTimeoutMs);

protected:
    void setToken(const OAuthToken& token);
    std::string getAuthHeader() const;

    std::string serviceName_;
    Models::StreamingServiceType serviceType_;
    OAuthToken token_;
    mutable std::mutex tokenMutex_;

    int maxRequestsPerSecond_ = 10;
    std::chrono::steady_clock::time_point lastRequestTime_;
    int requestCount_ = 0;
};

} // namespace BeatMate::Services::Streaming
