#pragma once
#include <vector>
#include <string>
#include "SuggestionOrchestrator.h"

namespace BeatMate::Services::Suggestions {

enum class EventType { WarmUp, PeakTime, AfterHours, Lounge, Festival };

struct PlayContext {
    EventType eventType = EventType::PeakTime;
    int hourOfDay = 22; // 0-23
    float targetEnergy = 7.0f;
    std::string preferredGenre;
};

class ContextualFilter {
public:
    ContextualFilter() = default;
    ~ContextualFilter() = default;

    std::vector<RecommendationResult> filter(const std::vector<RecommendationResult>& results, int count);
    void setContext(const PlayContext& context) { context_ = context; }
    PlayContext getContext() const { return context_; }

private:
    float contextScore(const Models::Track& track) const;
    PlayContext context_;
};

} // namespace BeatMate::Services::Suggestions
