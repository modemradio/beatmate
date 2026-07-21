#include "StreamingServiceBase.h"

#include <spdlog/spdlog.h>
#include <thread>

namespace BeatMate::Services::Streaming {

StreamingServiceBase::StreamingServiceBase(const std::string& serviceName, Models::StreamingServiceType type)
    : serviceName_(serviceName)
    , serviceType_(type)
    , lastRequestTime_(std::chrono::steady_clock::now()) {
}

void StreamingServiceBase::logout() {
    std::lock_guard<std::mutex> lock(tokenMutex_);
    token_ = OAuthToken();
    spdlog::info("{}: Logged out", serviceName_);
}

bool StreamingServiceBase::isAuthenticated() const {
    std::lock_guard<std::mutex> lock(tokenMutex_);
    return !token_.accessToken.empty() && !token_.isExpired();
}

bool StreamingServiceBase::canMakeRequest() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRequestTime_).count();

    if (elapsed >= 1000) {
        requestCount_ = 0;
        lastRequestTime_ = now;
    }

    if (requestCount_ >= maxRequestsPerSecond_) {
        int waitMs = static_cast<int>(1000 - elapsed);
        if (waitMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
        }
        requestCount_ = 0;
        lastRequestTime_ = std::chrono::steady_clock::now();
    }

    requestCount_++;
    return true;
}

void StreamingServiceBase::setToken(const OAuthToken& token) {
    std::lock_guard<std::mutex> lock(tokenMutex_);
    token_ = token;
    spdlog::info("{}: Token set, expires at {}", serviceName_, token.expiresAt);
}

OAuthToken StreamingServiceBase::getToken() const {
    std::lock_guard<std::mutex> lock(tokenMutex_);
    return token_;
}

std::string StreamingServiceBase::getAuthHeader() const {
    std::lock_guard<std::mutex> lock(tokenMutex_);
    return token_.tokenType + " " + token_.accessToken;
}

HttpResponse StreamingServiceBase::httpGet(const std::string& endpoint,
                                           const juce::String& extraHeaders,
                                           int timeoutMs,
                                           int softRetries) {
    HttpResponse last;
    const int attempts = 1 + (softRetries > 0 ? softRetries : 0);
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (attempt > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(300 * attempt));

        juce::URL url(endpoint);
        int status = 0;
        auto stream = url.createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs(timeoutMs)
                .withExtraHeaders(extraHeaders)
                .withStatusCode(&status));

        last = HttpResponse{};
        last.status = status;
        if (stream) {
            last.transportOk = true;
            last.body = stream->readEntireStreamAsString().toStdString();
        }

        if (last.ok()) return last;

        const bool retryable = !last.transportOk
                            || status == 429
                            || (status >= 500 && status < 600);
        if (!retryable) break;
        spdlog::warn("StreamingHttp: GET {} failed (status={}, transport={}), attempt {}/{}",
                     endpoint, status, last.transportOk, attempt + 1, attempts);
    }
    return last;
}

HttpResponse StreamingServiceBase::httpSend(const std::string& endpoint,
                                            const juce::String& verb,
                                            const std::string& body,
                                            const juce::String& extraHeaders,
                                            int timeoutMs) {
    juce::URL url(endpoint);
    if (!body.empty()) url = url.withPOSTData(body);

    int status = 0;
    auto stream = url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
            .withHttpRequestCmd(verb)
            .withConnectionTimeoutMs(timeoutMs)
            .withExtraHeaders(extraHeaders)
            .withStatusCode(&status));

    HttpResponse r;
    r.status = status;
    if (stream) {
        r.transportOk = true;
        r.body = stream->readEntireStreamAsString().toStdString();
    }
    if (!r.ok())
        spdlog::warn("StreamingHttp: {} {} failed (status={}, transport={})",
                     verb.toStdString(), endpoint, status, r.transportOk);
    return r;
}

} // namespace BeatMate::Services::Streaming
