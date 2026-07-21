#pragma once
#include <string>
#include <vector>
#include <memory>
#include "SuggestionOrchestrator.h"
#include "../../models/Track.h"

namespace BeatMate::Services::Library { class TrackDatabase; }

namespace BeatMate::Services::Suggestions {

struct ProSuggestion {
    Models::Track track;
    float score = 0.0f;
    float mixability = 0.0f;
    float crowdImpact = 0.0f;
    float technicalFit = 0.0f;
    std::string transitionAdvice;
    std::string mixTechnique;
    float beatGridAlignment = 0.0f;
};

class UltraProSuggestionEngine {
public:
    explicit UltraProSuggestionEngine(std::shared_ptr<Library::TrackDatabase> database);
    ~UltraProSuggestionEngine() = default;

    std::vector<ProSuggestion> suggest(const Models::Track& current, int count = 10);
    std::vector<ProSuggestion> suggestForMoment(const Models::Track& current, float targetEnergy, int count = 10);

private:
    float calculateMixability(const Models::Track& a, const Models::Track& b) const;
    float calculateCrowdImpact(const Models::Track& candidate, float targetEnergy) const;
    float calculateTechnicalFit(const Models::Track& a, const Models::Track& b) const;
    float calculateBeatGridAlignment(const Models::Track& a, const Models::Track& b) const;
    std::string recommendMixTechnique(float mixability, float bpmDiff, bool keyCompatible) const;
    bool isKeyCompatible(const std::string& key1, const std::string& key2) const;

    std::shared_ptr<Library::TrackDatabase> database_;
};

} // namespace BeatMate::Services::Suggestions
