#pragma once
#include <string>
#include <vector>
#include <map>
#include "../../models/Track.h"

namespace BeatMate::Services::Preparation {

struct BpmDistribution {
    double min = 0.0;
    double max = 0.0;
    double mean = 0.0;
    double median = 0.0;
    double stddev = 0.0;
    std::map<int, int> histogram; // bpm_bucket -> count
};

struct EnergyDistribution {
    float min = 0.0f;
    float max = 0.0f;
    float mean = 0.0f;
    float median = 0.0f;
    std::vector<float> curve; // energy values in order
};

struct GenreDistribution {
    std::map<std::string, int> counts;
    std::map<std::string, float> percentages;
    std::string dominant;
    int uniqueGenres = 0;
};

struct TransitionStats {
    int totalTransitions = 0;
    int compatibleBpm = 0;
    int compatibleKey = 0;
    int smoothEnergy = 0;
    float avgBpmDiff = 0.0f;
    float avgEnergyDiff = 0.0f;
    float compatibilityRate = 0.0f;
};

struct SetStatistics {
    int trackCount = 0;
    double totalDuration = 0.0;
    std::string totalDurationFormatted;
    BpmDistribution bpm;
    EnergyDistribution energy;
    GenreDistribution genres;
    TransitionStats transitions;
    int tracksWithKeys = 0;
    int tracksWithBpm = 0;
    int tracksAnalyzed = 0;
    double avgTrackDuration = 0.0;
};

class SetStatisticsService {
public:
    SetStatisticsService() = default;

    SetStatistics computeStatistics(const std::vector<Models::Track>& tracks);
    BpmDistribution computeBpmDistribution(const std::vector<Models::Track>& tracks);
    EnergyDistribution computeEnergyDistribution(const std::vector<Models::Track>& tracks);
    GenreDistribution computeGenreDistribution(const std::vector<Models::Track>& tracks);
    TransitionStats computeTransitionStats(const std::vector<Models::Track>& tracks);

private:
    std::string formatDuration(double seconds) const;
    bool isKeyCompatible(const std::string& key1, const std::string& key2) const;
};

} // namespace BeatMate::Services::Preparation
