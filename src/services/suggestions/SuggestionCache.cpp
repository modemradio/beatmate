#include "SuggestionCache.h"
#include <spdlog/spdlog.h>

namespace BeatMate::Services::Suggestions {

void SuggestionCache::cache(int64_t trackId, const std::vector<RecommendationResult>& suggestions) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cache_.size() >= maxSize_) {
        cache_.erase(cache_.begin()); // Evict oldest
    }
    cache_[trackId] = suggestions;
}

std::vector<RecommendationResult> SuggestionCache::get(int64_t trackId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(trackId);
    return (it != cache_.end()) ? it->second : std::vector<RecommendationResult>{};
}

bool SuggestionCache::has(int64_t trackId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.find(trackId) != cache_.end();
}

void SuggestionCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
    spdlog::info("SuggestionCache: Cleared");
}

void SuggestionCache::evict(int64_t trackId) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.erase(trackId);
}

size_t SuggestionCache::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.size();
}

} // namespace BeatMate::Services::Suggestions
