#pragma once
#include <string>
#include <vector>
#include <memory>
#include <map>
#include "SuggestionOrchestrator.h"
#include "../../models/Track.h"

namespace BeatMate::Services::Library { class TrackDatabase; }

namespace BeatMate::Services::Suggestions {

enum class SuggestionMode { Quick, Standard, Deep, Expert };

struct UnifiedSuggestion {
    Models::Track track;
    float score = 0.0f;
    std::string source;
    std::map<std::string, float> scores;
    std::string reason;
    std::string transitionHint;
};

struct UnifiedRequest {
    Models::Track referenceTrack;
    SuggestionMode mode = SuggestionMode::Standard;
    int count = 10;
    float bpmTolerance = 6.0f;
    float energyTolerance = 3.0f;
    bool onlyCompatibleKeys = false;
    std::string preferredGenre;
    std::vector<int64_t> excludeIds;
};

class UnifiedSuggestionService {
public:
    explicit UnifiedSuggestionService(std::shared_ptr<Library::TrackDatabase> database);
    ~UnifiedSuggestionService() = default;

    std::vector<UnifiedSuggestion> suggest(const UnifiedRequest& request);
    std::vector<UnifiedSuggestion> quickSuggest(const Models::Track& current, int count = 5);
    std::vector<UnifiedSuggestion> deepSuggest(const Models::Track& current, int count = 20);

private:
    std::vector<UnifiedSuggestion> quickMode(const UnifiedRequest& request);
    std::vector<UnifiedSuggestion> standardMode(const UnifiedRequest& request);
    std::vector<UnifiedSuggestion> deepMode(const UnifiedRequest& request);
    float computeScore(const Models::Track& ref, const Models::Track& cand) const;
    bool passesFilter(const Models::Track& candidate, const UnifiedRequest& request) const;
    bool isKeyCompatible(const std::string& key1, const std::string& key2) const;

    std::shared_ptr<Library::TrackDatabase> database_;
};

} // namespace BeatMate::Services::Suggestions
