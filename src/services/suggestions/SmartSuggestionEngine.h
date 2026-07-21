#pragma once
#include <string>
#include <vector>
#include <memory>
#include "SuggestionOrchestrator.h"
#include "../../models/Track.h"

namespace BeatMate::Services::Library { class TrackDatabase; }

namespace BeatMate::Services::Suggestions {

class SmartSuggestionEngine {
public:
    explicit SmartSuggestionEngine(std::shared_ptr<Library::TrackDatabase> database);
    ~SmartSuggestionEngine() = default;

    std::vector<RecommendationResult> suggest(const Models::Track& current, int count = 10);

    void setBpmTolerance(double percent) { bpmTolerance_ = percent; }
    void setEnergyTolerance(float tolerance) { energyTolerance_ = tolerance; }

private:
    bool isBpmCompatible(double bpm1, double bpm2) const;
    bool isKeyCompatible(const std::string& key1, const std::string& key2) const;
    float calculateScore(const Models::Track& current, const Models::Track& candidate) const;

    std::shared_ptr<Library::TrackDatabase> database_;
    double bpmTolerance_ = 5.0;
    float energyTolerance_ = 2.0f;
};

}
