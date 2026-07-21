#pragma once
#include <string>
#include <vector>
#include <memory>
#include "SuggestionOrchestrator.h"
#include "../../models/Track.h"

namespace BeatMate::Services::Library { class TrackDatabase; }

namespace BeatMate::Services::Suggestions {

struct AdvancedSuggestionConfig {
    bool useHarmonicAnalysis = true;
    bool useEnergyFlow = true;
    bool useGenreMatching = true;
    bool useMoodAnalysis = true;
    bool useTemporalContext = true;
    float minScore = 0.3f;
    int maxResults = 20;
};

struct AdvancedResult {
    RecommendationResult base;
    float harmonicQuality = 0.0f;
    float energyFlowQuality = 0.0f;
    float genreMatch = 0.0f;
    float moodMatch = 0.0f;
    std::string analysis;
};

class AdvancedSuggestionService {
public:
    explicit AdvancedSuggestionService(std::shared_ptr<Library::TrackDatabase> database);
    ~AdvancedSuggestionService() = default;

    std::vector<AdvancedResult> suggest(const Models::Track& current, const AdvancedSuggestionConfig& config = {});
    std::vector<AdvancedResult> suggestForPlaylist(const std::vector<Models::Track>& playlist, int count = 10);
    std::vector<AdvancedResult> suggestSimilar(const Models::Track& reference, int count = 10);

private:
    AdvancedResult analyzeCandidate(const Models::Track& current, const Models::Track& candidate,
                                     const AdvancedSuggestionConfig& config) const;
    bool isKeyCompatible(const std::string& key1, const std::string& key2) const;

    std::shared_ptr<Library::TrackDatabase> database_;
};

} // namespace BeatMate::Services::Suggestions
