#pragma once
#include <string>
#include <vector>
#include <functional>
#include "../../models/Track.h"

namespace BeatMate::Services::Preparation {

struct PlannerConfig {
    double targetDurationMinutes = 60.0;
    float bpmWeight = 0.3f;
    float keyWeight = 0.3f;
    float energyWeight = 0.2f;
    float genreWeight = 0.2f;
    int maxIterations = 1000;
    float temperatureStart = 100.0f;
    float temperatureCooling = 0.995f;
};

struct PlannerResult {
    std::vector<Models::Track> orderedTracks;
    float totalScore = 0.0f;
    float avgTransitionScore = 0.0f;
    int iterations = 0;
    double totalDuration = 0.0;
};

class SetPlannerEngine {
public:
    SetPlannerEngine() = default;

    PlannerResult planOptimal(const std::vector<Models::Track>& tracks, const PlannerConfig& config);
    PlannerResult planNearestNeighbor(const std::vector<Models::Track>& tracks, const PlannerConfig& config);
    PlannerResult planSimulatedAnnealing(const std::vector<Models::Track>& tracks, const PlannerConfig& config);
    PlannerResult planOptimal2Opt(const std::vector<Models::Track>& tracks, const PlannerConfig& config, int maxIterations = 2000);

    float calculatePathScore(const std::vector<Models::Track>& order, const PlannerConfig& config) const;

private:
    float transitionScore(const Models::Track& from, const Models::Track& to, const PlannerConfig& config) const;
    float bpmScore(double bpm1, double bpm2) const;
    float keyScore(const std::string& key1, const std::string& key2) const;
    float energyScore(float e1, float e2) const;
    float genreScore(const std::string& g1, const std::string& g2) const;
    PlannerResult buildResult(const std::vector<Models::Track>& order, const PlannerConfig& config, int iterations);
};

} // namespace BeatMate::Services::Preparation
