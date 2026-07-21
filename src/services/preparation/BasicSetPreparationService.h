#pragma once
#include <string>
#include <vector>
#include "../../models/Track.h"

namespace BeatMate::Services::Preparation {

struct QuickSetConfig {
    double targetDurationMinutes = 60.0;
    double bpmMin = 0.0;
    double bpmMax = 0.0;
    std::string genre;
    float energyMin = 0.0f;
    float energyMax = 10.0f;
    bool sortByBpm = true;
    bool sortByEnergy = false;
};

struct QuickSetResult {
    std::string name;
    std::vector<Models::Track> tracks;
    double totalDuration = 0.0;
    double avgBpm = 0.0;
    float avgEnergy = 0.0f;
    int trackCount = 0;
};

class BasicSetPreparationService {
public:
    BasicSetPreparationService() = default;

    QuickSetResult prepareQuickSet(const std::vector<Models::Track>& pool, const QuickSetConfig& config);
    QuickSetResult prepareByGenre(const std::vector<Models::Track>& pool, const std::string& genre, double durationMinutes);
    QuickSetResult prepareByBpmRange(const std::vector<Models::Track>& pool, double bpmMin, double bpmMax, double durationMinutes);
    QuickSetResult prepareByEnergyRange(const std::vector<Models::Track>& pool, float energyMin, float energyMax, double durationMinutes);

private:
    std::vector<Models::Track> filterTracks(const std::vector<Models::Track>& pool, const QuickSetConfig& config);
    void sortTracks(std::vector<Models::Track>& tracks, const QuickSetConfig& config);
    std::vector<Models::Track> selectForDuration(const std::vector<Models::Track>& tracks, double targetSeconds);
    QuickSetResult buildResult(const std::string& name, const std::vector<Models::Track>& tracks);
};

} // namespace BeatMate::Services::Preparation
