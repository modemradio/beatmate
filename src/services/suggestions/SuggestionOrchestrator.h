#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>

#include "../../models/Track.h"

namespace BeatMate::Services::Suggestions {

struct RecommendationResult {
    Models::Track track;
    float score = 0.0f;        // 0-1 overall score
    std::string reason;         // e.g. "Compatible key (8A -> 9A)"
    std::map<std::string, float> componentScores; // bpm, key, energy, etc.
};

class SmartSuggestionEngine;
class HarmonicSuggestionEngine;
class MLSuggestionEngine;
class HistoryAnalysisEngine;
class ContextualFilter;

class SuggestionOrchestrator {
public:
    SuggestionOrchestrator();
    ~SuggestionOrchestrator();

    std::vector<RecommendationResult> getSuggestions(const Models::Track& currentTrack, int count = 10);

    void setSmartEngine(std::shared_ptr<SmartSuggestionEngine> engine) { smartEngine_ = std::move(engine); }
    void setHarmonicEngine(std::shared_ptr<HarmonicSuggestionEngine> engine) { harmonicEngine_ = std::move(engine); }
    void setMLEngine(std::shared_ptr<MLSuggestionEngine> engine) { mlEngine_ = std::move(engine); }
    void setHistoryEngine(std::shared_ptr<HistoryAnalysisEngine> engine) { historyEngine_ = std::move(engine); }
    void setContextFilter(std::shared_ptr<ContextualFilter> filter) { contextFilter_ = std::move(filter); }

    // Weights for each engine (must sum to 1.0)
    void setWeights(float smart, float harmonic, float ml, float history);

private:
    std::vector<RecommendationResult> mergeResults(
        const std::vector<std::vector<RecommendationResult>>& allResults, int count);

    std::shared_ptr<SmartSuggestionEngine> smartEngine_;
    std::shared_ptr<HarmonicSuggestionEngine> harmonicEngine_;
    std::shared_ptr<MLSuggestionEngine> mlEngine_;
    std::shared_ptr<HistoryAnalysisEngine> historyEngine_;
    std::shared_ptr<ContextualFilter> contextFilter_;

    float weightSmart_ = 0.3f;
    float weightHarmonic_ = 0.3f;
    float weightML_ = 0.2f;
    float weightHistory_ = 0.2f;
};

} // namespace BeatMate::Services::Suggestions
