#include "DJStyleLearner.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <numeric>

namespace BeatMate::Services::AI {

DJStylePreferences DJStyleLearner::learn(const std::vector<Models::Track>& playHistory) {
    if (playHistory.empty()) return preferences_;

    std::vector<double> bpms;
    std::map<std::string, int> genreCounts, keyCounts;
    std::vector<float> energies;

    for (const auto& track : playHistory) {
        if (track.bpm > 0) bpms.push_back(track.bpm);
        if (!track.genre.empty()) genreCounts[track.genre]++;
        if (!track.key.empty()) keyCounts[track.key]++;
        if (track.energy > 0) energies.push_back(track.energy);
    }

    if (!bpms.empty()) {
        std::sort(bpms.begin(), bpms.end());
        size_t p10 = bpms.size() / 10, p90 = bpms.size() * 9 / 10;
        preferences_.preferredBpmMin = bpms[p10];
        preferences_.preferredBpmMax = bpms[std::min(p90, bpms.size() - 1)];
    }

    std::vector<std::pair<std::string, int>> genreVec(genreCounts.begin(), genreCounts.end());
    std::sort(genreVec.begin(), genreVec.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    for (size_t i = 0; i < std::min(genreVec.size(), size_t(5)); ++i) {
        preferences_.preferredGenres.push_back(genreVec[i].first);
        preferences_.genreWeights[genreVec[i].first] = static_cast<float>(genreVec[i].second) / playHistory.size();
    }

    std::vector<std::pair<std::string, int>> keyVec(keyCounts.begin(), keyCounts.end());
    std::sort(keyVec.begin(), keyVec.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    for (size_t i = 0; i < std::min(keyVec.size(), size_t(5)); ++i) {
        preferences_.preferredKeys.push_back(keyVec[i].first);
    }

    if (!energies.empty()) {
        std::sort(energies.begin(), energies.end());
        preferences_.preferredEnergyMin = energies[energies.size() / 10];
        preferences_.preferredEnergyMax = energies[energies.size() * 9 / 10];
    }

    spdlog::info("DJStyleLearner: Learned from {} tracks. BPM: {:.0f}-{:.0f}, Genres: {}",
                 playHistory.size(), preferences_.preferredBpmMin, preferences_.preferredBpmMax,
                 preferences_.preferredGenres.size());
    return preferences_;
}

} // namespace BeatMate::Services::AI
