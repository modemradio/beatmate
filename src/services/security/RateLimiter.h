#pragma once
#include <string>
#include <map>
#include <chrono>
#include <mutex>
namespace BeatMate::Services::Security {
class RateLimiter {
public:
    RateLimiter() = default;
    bool tryAcquire(const std::string& service);
    void setLimit(const std::string& service, int requestsPerSecond);
    void reset(const std::string& service);
private:
    struct Bucket { int maxTokens = 10; double tokens = 10; std::chrono::steady_clock::time_point lastRefill; };
    std::map<std::string, Bucket> buckets_;
    mutable std::mutex mutex_;
};
} // namespace BeatMate::Services::Security
