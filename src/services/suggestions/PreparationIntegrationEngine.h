#pragma once
#include <string>
#include <vector>
#include <memory>
#include "SuggestionOrchestrator.h"
#include "../../models/Track.h"
#include "../../models/SetPlan.h"
#include "../../models/EventPlan.h"

namespace BeatMate::Services::Library { class TrackDatabase; }

namespace BeatMate::Services::Suggestions {

struct PreparationSuggestion {
    RecommendationResult base;
    std::string context;            // "set_gap", "energy_fill", "genre_bridge", "key_chain"
    int suggestedPosition = -1;
    float setImprovementScore = 0.0f;
};

class PreparationIntegrationEngine {
public:
    explicit PreparationIntegrationEngine(std::shared_ptr<Library::TrackDatabase> database);
    ~PreparationIntegrationEngine() = default;

    std::vector<PreparationSuggestion> suggestForSet(const Models::SetPlan& set, int count = 10);
    std::vector<PreparationSuggestion> suggestForGap(const Models::Track& before, const Models::Track& after, int count = 5);
    std::vector<PreparationSuggestion> suggestForEvent(const Models::EventPlan& event,
                                                        const std::string& sectionName, int count = 10);
    std::vector<PreparationSuggestion> suggestToImproveEnergy(const Models::SetPlan& set,
                                                               float targetEnergy, int position, int count = 5);

private:
    float gapFitScore(const Models::Track& candidate, const Models::Track& before,
                       const Models::Track& after) const;
    float setImprovementScore(const Models::Track& candidate, const Models::SetPlan& set, int position) const;
    bool isKeyCompatible(const std::string& key1, const std::string& key2) const;

    std::shared_ptr<Library::TrackDatabase> database_;
};

}
