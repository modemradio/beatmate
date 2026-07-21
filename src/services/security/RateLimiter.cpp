#include <algorithm>
#include "RateLimiter.h"
#include <spdlog/spdlog.h>
namespace BeatMate::Services::Security {
bool RateLimiter::tryAcquire(const std::string& service) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& bucket = buckets_[service];
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - bucket.lastRefill).count();
    bucket.tokens = std::min(static_cast<double>(bucket.maxTokens), bucket.tokens + elapsed * bucket.maxTokens);
    bucket.lastRefill = now;
    if (bucket.tokens >= 1.0) { bucket.tokens -= 1.0; return true; }
    spdlog::debug("RateLimiter: {} rate limited", service);
    return false;
}
void RateLimiter::setLimit(const std::string& service, int requestsPerSecond) {
    std::lock_guard<std::mutex> lock(mutex_);
    buckets_[service].maxTokens = requestsPerSecond;
    buckets_[service].tokens = requestsPerSecond;
    buckets_[service].lastRefill = std::chrono::steady_clock::now();
}
void RateLimiter::reset(const std::string& service) {
    std::lock_guard<std::mutex> lock(mutex_);
    buckets_.erase(service);
}
}
