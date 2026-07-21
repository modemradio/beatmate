#pragma once
#include <string>
#include <vector>
#include <memory>
#include <map>
#include "SuggestionOrchestrator.h"
#include "../../models/Track.h"

namespace BeatMate::Services::Library { class TrackDatabase; }

namespace BeatMate::Services::Suggestions {

struct IntelligentContext {
    float timeOfDayFactor = 1.0f;      // energy modifier based on time
    float crowdEnergyFactor = 1.0f;    // crowd energy perception
    int setPosition = 0;               // position in the current set (0=start)
    std::vector<int64_t> recentlyPlayed;
    std::string preferredGenre;
    float energyDirection = 0.0f;      // -1=decreasing, 0=stable, 1=increasing
};

struct IntelligentSuggestion {
    Models::Track track;
    float score = 0.0f;
    float harmonicScore = 0.0f;
    float energyFlowScore = 0.0f;
    float contextScore = 0.0f;
    float noveltyScore = 0.0f;
    std::string primaryReason;
    std::vector<std::string> allReasons;
};

class IntelligentSuggestionEngine {
public:
    explicit IntelligentSuggestionEngine(std::shared_ptr<Library::TrackDatabase> database);
    ~IntelligentSuggestionEngine() = default;

    std::vector<IntelligentSuggestion> suggest(const Models::Track& current, int count = 10);
    std::vector<IntelligentSuggestion> suggestWithContext(const Models::Track& current,
                                                           const IntelligentContext& context, int count = 10);
    void setContext(const IntelligentContext& context) { context_ = context; }

private:
    float harmonicScore(const Models::Track& a, const Models::Track& b) const;
    float energyFlowScore(const Models::Track& current, const Models::Track& candidate) const;
    float contextualScore(const Models::Track& candidate, const IntelligentContext& context) const;
    float noveltyScore(const Models::Track& candidate, const IntelligentContext& context) const;
    bool isKeyCompatible(const std::string& key1, const std::string& key2) const;

    std::shared_ptr<Library::TrackDatabase> database_;
    IntelligentContext context_;
};

} // namespace BeatMate::Services::Suggestions
