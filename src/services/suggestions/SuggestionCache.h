#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <cstdint>
#include "SuggestionOrchestrator.h"

namespace BeatMate::Services::Suggestions {

class SuggestionCache {
public:
    SuggestionCache() = default;
    ~SuggestionCache() = default;

    void cache(int64_t trackId, const std::vector<RecommendationResult>& suggestions);
    std::vector<RecommendationResult> get(int64_t trackId);
    bool has(int64_t trackId) const;
    void clear();
    void evict(int64_t trackId);
    size_t size() const;
    void setMaxSize(size_t max) { maxSize_ = max; }

private:
    std::map<int64_t, std::vector<RecommendationResult>> cache_;
    mutable std::mutex mutex_;
    size_t maxSize_ = 1000;
};

} // namespace BeatMate::Services::Suggestions
