#pragma once
#include <string>
#include <vector>
#include <memory>
#include <map>
#include "SuggestionOrchestrator.h"
#include "../../models/Track.h"

namespace BeatMate::Services::Library { class TrackDatabase; }

namespace BeatMate::Services::Suggestions {

struct QuantumState {
    float harmonicProbability = 0.0f;
    float energyProbability = 0.0f;
    float rhythmicProbability = 0.0f;
    float culturalProbability = 0.0f;
    float collapsedScore = 0.0f;
};

struct QuantumSuggestion {
    Models::Track track;
    QuantumState state;
    float confidence = 0.0f;
    std::string quantumInsight;
    std::vector<std::pair<std::string, float>> probabilityMap;
};

class QuantumSuggestionEngine {
public:
    explicit QuantumSuggestionEngine(std::shared_ptr<Library::TrackDatabase> database);
    ~QuantumSuggestionEngine() = default;

    std::vector<QuantumSuggestion> suggest(const Models::Track& current, int count = 10);
    std::vector<QuantumSuggestion> superpositionAnalysis(const Models::Track& current,
                                                          const std::vector<Models::Track>& history, int count = 10);

private:
    QuantumState computeQuantumState(const Models::Track& current, const Models::Track& candidate,
                                      const std::vector<Models::Track>& history) const;
    float collapseState(const QuantumState& state) const;
    float harmonicProbability(const Models::Track& a, const Models::Track& b) const;
    float energyProbability(const Models::Track& a, const Models::Track& b) const;
    float rhythmicProbability(const Models::Track& a, const Models::Track& b) const;
    float culturalProbability(const Models::Track& candidate, const std::vector<Models::Track>& history) const;
    bool isKeyCompatible(const std::string& key1, const std::string& key2) const;

    std::shared_ptr<Library::TrackDatabase> database_;
};

} // namespace BeatMate::Services::Suggestions
