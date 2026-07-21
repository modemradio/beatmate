#include "SetStatisticsService.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <iomanip>

namespace BeatMate::Services::Preparation {

SetStatistics SetStatisticsService::computeStatistics(const std::vector<Models::Track>& tracks) {
    SetStatistics stats;
    stats.trackCount = static_cast<int>(tracks.size());
    stats.totalDuration = 0.0;

    for (const auto& t : tracks) {
        stats.totalDuration += t.duration;
        if (t.bpm > 0) ++stats.tracksWithBpm;
        if (!t.camelotKey.empty() || !t.key.empty()) ++stats.tracksWithKeys;
        if (t.analyzed) ++stats.tracksAnalyzed;
    }

    stats.totalDurationFormatted = formatDuration(stats.totalDuration);
    stats.avgTrackDuration = stats.trackCount > 0 ? stats.totalDuration / stats.trackCount : 0.0;
    stats.bpm = computeBpmDistribution(tracks);
    stats.energy = computeEnergyDistribution(tracks);
    stats.genres = computeGenreDistribution(tracks);
    stats.transitions = computeTransitionStats(tracks);

    spdlog::info("SetStatisticsService: {} tracks, {} total, avg BPM={:.1f}, {} genres",
                 stats.trackCount, stats.totalDurationFormatted, stats.bpm.mean, stats.genres.uniqueGenres);
    return stats;
}

BpmDistribution SetStatisticsService::computeBpmDistribution(const std::vector<Models::Track>& tracks) {
    BpmDistribution dist;
    std::vector<double> bpms;
    for (const auto& t : tracks) {
        if (t.bpm > 0) bpms.push_back(t.bpm);
    }
    if (bpms.empty()) return dist;

    std::sort(bpms.begin(), bpms.end());
    dist.min = bpms.front();
    dist.max = bpms.back();
    dist.mean = std::accumulate(bpms.begin(), bpms.end(), 0.0) / static_cast<double>(bpms.size());
    dist.median = bpms[bpms.size() / 2];

    double variance = 0.0;
    for (double b : bpms) variance += (b - dist.mean) * (b - dist.mean);
    dist.stddev = std::sqrt(variance / static_cast<double>(bpms.size()));

    for (double b : bpms) {
        int bucket = static_cast<int>(b / 5.0) * 5;
        dist.histogram[bucket]++;
    }

    return dist;
}

EnergyDistribution SetStatisticsService::computeEnergyDistribution(const std::vector<Models::Track>& tracks) {
    EnergyDistribution dist;
    std::vector<float> energies;
    for (const auto& t : tracks) {
        energies.push_back(t.energy);
        dist.curve.push_back(t.energy);
    }
    if (energies.empty()) return dist;

    std::sort(energies.begin(), energies.end());
    dist.min = energies.front();
    dist.max = energies.back();
    dist.mean = std::accumulate(energies.begin(), energies.end(), 0.0f) / static_cast<float>(energies.size());
    dist.median = energies[energies.size() / 2];
    return dist;
}

GenreDistribution SetStatisticsService::computeGenreDistribution(const std::vector<Models::Track>& tracks) {
    GenreDistribution dist;
    for (const auto& t : tracks) {
        std::string genre = t.genre.empty() ? "Unknown" : t.genre;
        dist.counts[genre]++;
    }
    dist.uniqueGenres = static_cast<int>(dist.counts.size());

    int maxCount = 0;
    for (const auto& [genre, count] : dist.counts) {
        dist.percentages[genre] = static_cast<float>(count) / static_cast<float>(tracks.size()) * 100.0f;
        if (count > maxCount) {
            maxCount = count;
            dist.dominant = genre;
        }
    }
    return dist;
}

TransitionStats SetStatisticsService::computeTransitionStats(const std::vector<Models::Track>& tracks) {
    TransitionStats stats;
    if (tracks.size() < 2) return stats;

    stats.totalTransitions = static_cast<int>(tracks.size()) - 1;
    float totalBpmDiff = 0.0f;
    float totalEnergyDiff = 0.0f;

    for (size_t i = 1; i < tracks.size(); ++i) {
        double bpmDiff = std::abs(tracks[i].bpm - tracks[i - 1].bpm);
        float energyDiff = std::abs(tracks[i].energy - tracks[i - 1].energy);
        totalBpmDiff += static_cast<float>(bpmDiff);
        totalEnergyDiff += energyDiff;

        if (bpmDiff <= 6.0) ++stats.compatibleBpm;
        if (energyDiff <= 2.0f) ++stats.smoothEnergy;

        std::string key1 = tracks[i - 1].camelotKey.empty() ? tracks[i - 1].key : tracks[i - 1].camelotKey;
        std::string key2 = tracks[i].camelotKey.empty() ? tracks[i].key : tracks[i].camelotKey;
        if (!key1.empty() && !key2.empty() && isKeyCompatible(key1, key2)) {
            ++stats.compatibleKey;
        }
    }

    stats.avgBpmDiff = totalBpmDiff / static_cast<float>(stats.totalTransitions);
    stats.avgEnergyDiff = totalEnergyDiff / static_cast<float>(stats.totalTransitions);
    int compatible = stats.compatibleBpm + stats.compatibleKey + stats.smoothEnergy;
    stats.compatibilityRate = static_cast<float>(compatible) / static_cast<float>(stats.totalTransitions * 3);
    return stats;
}

bool SetStatisticsService::isKeyCompatible(const std::string& key1, const std::string& key2) const {
    if (key1 == key2) return true;
    if (key1.size() < 2 || key2.size() < 2) return false;
    try {
        int num1 = std::stoi(key1.substr(0, key1.size() - 1));
        int num2 = std::stoi(key2.substr(0, key2.size() - 1));
        char let1 = key1.back();
        char let2 = key2.back();
        if (num1 == num2 && let1 != let2) return true;
        int diff = ((num2 - num1) + 12) % 12;
        if ((diff == 1 || diff == 11) && let1 == let2) return true;
    } catch (...) {}
    return false;
}

std::string SetStatisticsService::formatDuration(double seconds) const {
    int h = static_cast<int>(seconds) / 3600;
    int m = (static_cast<int>(seconds) % 3600) / 60;
    int s = static_cast<int>(seconds) % 60;
    std::ostringstream ss;
    if (h > 0) ss << h << "h ";
    ss << std::setfill('0') << std::setw(2) << m << "m " << std::setw(2) << s << "s";
    return ss.str();
}

} // namespace BeatMate::Services::Preparation
