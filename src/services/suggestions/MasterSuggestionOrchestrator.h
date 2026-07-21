#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include "SuggestionOrchestrator.h"
#include "IntelligentSuggestionEngine.h"
#include "HyperIntelligentSuggestionEngine.h"
#include "UltraProSuggestionEngine.h"
#include "QuantumSuggestionEngine.h"
#include "../../models/Track.h"

namespace BeatMate::Services::Library { class TrackDatabase; }

namespace BeatMate::Services::Suggestions {

class MyStyleModel;

struct MasterSuggestion {
    Models::Track track;
    float finalScore = 0.0f;
    std::map<std::string, float> engineScores;
    std::string bestEngine;
    std::string recommendation;
    float consensus = 0.0f; // how much engines agree
};

struct MasterConfig {
    float intelligentWeight = 0.25f;
    float hyperWeight = 0.25f;
    float proWeight = 0.25f;
    float quantumWeight = 0.25f;
    bool enableAll = true;
    int maxResults = 20;
};

class MasterSuggestionOrchestrator {
public:
    explicit MasterSuggestionOrchestrator(std::shared_ptr<Library::TrackDatabase> database);
    ~MasterSuggestionOrchestrator() = default;

    std::vector<MasterSuggestion> orchestrate(const Models::Track& current, int count = 10);
    std::vector<MasterSuggestion> orchestrateWithConfig(const Models::Track& current, const MasterConfig& config);
    void setConfig(const MasterConfig& config) { config_ = config; }

    // Prior de personnalisation applique au score final quand entraine
    void setMyStyleModel(MyStyleModel* model)   { myStyle_       = model;  }
    void setMyStylePriorWeight(float w)         { myStyleWeight_ = w;      }

private:
    std::vector<MasterSuggestion> mergeEngineResults(
        const std::map<std::string, std::vector<std::pair<int64_t, float>>>& engineResults,
        const std::map<int64_t, Models::Track>& trackMap) const;

    std::shared_ptr<Library::TrackDatabase> database_;
    std::unique_ptr<IntelligentSuggestionEngine> intelligentEngine_;
    std::unique_ptr<HyperIntelligentSuggestionEngine> hyperEngine_;
    std::unique_ptr<UltraProSuggestionEngine> proEngine_;
    std::unique_ptr<QuantumSuggestionEngine> quantumEngine_;
    MasterConfig config_;

    MyStyleModel* myStyle_       = nullptr;
    float         myStyleWeight_ = 0.35f;
};

} // namespace BeatMate::Services::Suggestions
