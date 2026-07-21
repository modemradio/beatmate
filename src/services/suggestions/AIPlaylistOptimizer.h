#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "../../models/Track.h"

namespace BeatMate::Services::Library { class TrackDatabase; }

namespace BeatMate::Services::Suggestions {

struct OptimizationGoal {
    float smoothBpmWeight = 0.3f;
    float harmonicMixWeight = 0.3f;
    float energyArcWeight = 0.25f;
    float genreCohesionWeight = 0.15f;
    float targetEnergyStart = 3.0f;
    float targetEnergyPeak = 8.0f;
    float targetEnergyEnd = 4.0f;
};

struct OptimizationResult {
    std::vector<Models::Track> optimizedPlaylist;
    float scoreBefore = 0.0f;
    float scoreAfter = 0.0f;
    float improvement = 0.0f;
    int swapsMade = 0;
    int tracksAdded = 0;
    int tracksRemoved = 0;
    std::string summary;
};

using OptimizationProgressCallback = std::function<void(float progress, const std::string& step)>;

class AIPlaylistOptimizer {
public:
    explicit AIPlaylistOptimizer(std::shared_ptr<Library::TrackDatabase> database);
    ~AIPlaylistOptimizer() = default;

    OptimizationResult optimize(const std::vector<Models::Track>& playlist, const OptimizationGoal& goal);
    OptimizationResult optimizeWithProgress(const std::vector<Models::Track>& playlist,
                                              const OptimizationGoal& goal, OptimizationProgressCallback callback);
    OptimizationResult reorder(const std::vector<Models::Track>& playlist, const OptimizationGoal& goal);
    OptimizationResult fillGaps(const std::vector<Models::Track>& playlist, const OptimizationGoal& goal);
    float evaluatePlaylist(const std::vector<Models::Track>& playlist, const OptimizationGoal& goal) const;

private:
    float bpmFlowScore(const std::vector<Models::Track>& playlist) const;
    float harmonicScore(const std::vector<Models::Track>& playlist) const;
    float energyArcScore(const std::vector<Models::Track>& playlist, const OptimizationGoal& goal) const;
    float genreCohesionScore(const std::vector<Models::Track>& playlist) const;
    bool isKeyCompatible(const std::string& key1, const std::string& key2) const;
    float targetEnergyAt(float position, const OptimizationGoal& goal) const;

    std::shared_ptr<Library::TrackDatabase> database_;
};

} // namespace BeatMate::Services::Suggestions
