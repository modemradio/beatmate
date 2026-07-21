#pragma once
#include <string>
#include <vector>
#include <memory>
#include "SuggestionOrchestrator.h"
#include "../../models/Track.h"
#include "../../models/StreamingTrack.h"

namespace BeatMate::Services::Library { class TrackDatabase; }

namespace BeatMate::Services::Suggestions {

struct StreamingSuggestion {
    Models::Track track;
    float score = 0.0f;
    std::string streamingService;  // "spotify", "tidal", "apple_music", etc.
    std::string streamingUrl;
    bool availableOffline = false;
    float popularity = 0.0f;
    std::string reason;
};

struct StreamingSearchConfig {
    std::vector<std::string> enabledServices;
    bool preferLocal = true;
    bool includeStreamingOnly = false;
    int maxPerService = 5;
};

class StreamingSuggestionService {
public:
    explicit StreamingSuggestionService(std::shared_ptr<Library::TrackDatabase> database);
    ~StreamingSuggestionService() = default;

    std::vector<StreamingSuggestion> suggest(const Models::Track& current, int count = 10);
    std::vector<StreamingSuggestion> suggestFromStreaming(const Models::Track& current,
                                                           const StreamingSearchConfig& config, int count = 10);
    std::vector<StreamingSuggestion> mergeLocalAndStreaming(const Models::Track& current, int count = 10);

private:
    std::vector<StreamingSuggestion> getLocalSuggestions(const Models::Track& current, int count);
    float computeScore(const Models::Track& ref, const Models::Track& cand) const;
    bool isKeyCompatible(const std::string& key1, const std::string& key2) const;

    std::shared_ptr<Library::TrackDatabase> database_;
};

} // namespace BeatMate::Services::Suggestions
