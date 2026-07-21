#pragma once
#include <string>
#include <vector>
#include <memory>
#include "SuggestionOrchestrator.h"
#include "../../models/Track.h"
#include "../../models/TrackAnalysis.h"

namespace BeatMate::Services::Library { class TrackDatabase; }

namespace BeatMate::Services::Suggestions {

struct DeepAnalysisFactors {
    float spectralSimilarity = 0.0f;
    float rhythmicSimilarity = 0.0f;
    float structuralCompatibility = 0.0f;
    float timbralMatch = 0.0f;
    float historicalSuccess = 0.0f;
};

struct HyperSuggestion {
    Models::Track track;
    float score = 0.0f;
    DeepAnalysisFactors factors;
    std::string insight;
    float confidence = 0.0f;
};

class HyperIntelligentSuggestionEngine {
public:
    explicit HyperIntelligentSuggestionEngine(std::shared_ptr<Library::TrackDatabase> database);
    ~HyperIntelligentSuggestionEngine() = default;

    std::vector<HyperSuggestion> suggest(const Models::Track& current, int count = 10);
    std::vector<HyperSuggestion> deepAnalyze(const Models::Track& current, const Models::TrackAnalysis& analysis, int count = 10);

private:
    float spectralSimilarity(const Models::Track& a, const Models::Track& b) const;
    float rhythmicSimilarity(const Models::Track& a, const Models::Track& b) const;
    float structuralCompatibility(const Models::Track& a, const Models::Track& b) const;
    float timbralMatch(const Models::Track& a, const Models::Track& b) const;
    float historicalSuccess(const Models::Track& candidate) const;
    std::string generateInsight(const DeepAnalysisFactors& factors) const;
    bool isKeyCompatible(const std::string& key1, const std::string& key2) const;

    std::shared_ptr<Library::TrackDatabase> database_;
};

} // namespace BeatMate::Services::Suggestions
