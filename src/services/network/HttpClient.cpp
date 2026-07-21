#include "HttpClient.h"
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>

namespace BeatMate::Services::Network {

HttpResponse HttpClient::get(const std::string& url, const std::map<std::string, std::string>& headers) {
    return execute("GET", url, "", headers);
}

HttpResponse HttpClient::post(const std::string& url, const std::string& body, const std::map<std::string, std::string>& headers) {
    return execute("POST", url, body, headers);
}

HttpResponse HttpClient::put(const std::string& url, const std::string& body, const std::map<std::string, std::string>& headers) {
    return execute("PUT", url, body, headers);
}

HttpResponse HttpClient::del(const std::string& url, const std::map<std::string, std::string>& headers) {
    return execute("DELETE", url, "", headers);
}

void HttpClient::getAsync(const std::string& url, HttpCallback callback, const std::map<std::string, std::string>& headers) {
    HttpClient snapshot = *this;
    std::thread([snapshot, url, callback, headers]() mutable {
        auto response = snapshot.get(url, headers);
        if (callback) callback(response);
    }).detach();
}

HttpResponse HttpClient::execute(const std::string& method, const std::string& url,
                                  const std::string& body, const std::map<std::string, std::string>& headers) {
    HttpResponse response;

    for (int attempt = 0; attempt <= maxRetries_; ++attempt) {
        try {
            juce::URL juceUrl{juce::String(url)};

            // For POST/PUT, attach the body as post data
            if (!body.empty() && (method == "POST" || method == "PUT")) {
                juceUrl = juceUrl.withPOSTData(juce::String(body));
            }

            // Build header string
            juce::String extraHeaders;
            for (const auto& [k, v] : headers) {
                extraHeaders += juce::String(k) + ": " + juce::String(v) + "\r\n";
            }

            // Determine if POST
            bool isPost = (method == "POST" || method == "PUT" || method == "DELETE");

            // Create the input stream
            auto stream = juceUrl.createInputStream(
                juce::URL::InputStreamOptions(
                    isPost ? juce::URL::ParameterHandling::inPostData
                           : juce::URL::ParameterHandling::inAddress)
                    .withExtraHeaders(extraHeaders)
                    .withConnectionTimeoutMs(timeoutMs_)
                    .withHttpRequestCmd(juce::String(method)));

            if (stream) {
                // Read response body
                response.body = stream->readEntireStreamAsString().toStdString();

                // Get status code from the stream
                if (auto* httpStream = dynamic_cast<juce::WebInputStream*>(stream.get())) {
                    response.statusCode = httpStream->getStatusCode();

                    // Parse response headers
                    auto responseHeaders = httpStream->getResponseHeaders();
                    auto keys = responseHeaders.getAllKeys();
                    auto values = responseHeaders.getAllValues();
                    for (int i = 0; i < keys.size(); ++i) {
                        response.headers[keys[i].toStdString()] = values[i].toStdString();
                    }
                }

                response.success = (response.statusCode >= 200 && response.statusCode < 300);

                if (response.success || response.statusCode < 500) {
                    return response;
                }
            } else {
                response.error = "Failed to create input stream";
                response.success = false;
            }

        } catch (const std::exception& e) {
            response.error = e.what();
            response.success = false;
        }

        if (attempt < maxRetries_) {
            int waitMs = 1000 * (attempt + 1);
            spdlog::debug("HttpClient: Retry {} after {}ms for {}", attempt + 1, waitMs, url);
            std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
        }
    }

    return response;
}

} // namespace BeatMate::Services::Network
