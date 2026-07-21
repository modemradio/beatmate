#pragma once
#include <juce_core/juce_core.h>
#include <string>
#include <map>
#include <functional>
#include <thread>

namespace BeatMate::Services::Network {

struct HttpResponse {
    int statusCode = 0;
    std::string body;
    std::map<std::string, std::string> headers;
    bool success = false;
    std::string error;
};

using HttpCallback = std::function<void(const HttpResponse&)>;

class HttpClient {
public:
    HttpClient() = default;
    ~HttpClient() = default;

    HttpResponse get(const std::string& url, const std::map<std::string, std::string>& headers = {});
    HttpResponse post(const std::string& url, const std::string& body, const std::map<std::string, std::string>& headers = {});
    HttpResponse put(const std::string& url, const std::string& body, const std::map<std::string, std::string>& headers = {});
    HttpResponse del(const std::string& url, const std::map<std::string, std::string>& headers = {});

    void getAsync(const std::string& url, HttpCallback callback, const std::map<std::string, std::string>& headers = {});

    void setMaxRetries(int retries) { maxRetries_ = retries; }
    void setTimeout(int ms) { timeoutMs_ = ms; }

private:
    HttpResponse execute(const std::string& method, const std::string& url,
                         const std::string& body, const std::map<std::string, std::string>& headers);

    int maxRetries_ = 3;
    int timeoutMs_ = 30000;
};

} // namespace BeatMate::Services::Network
