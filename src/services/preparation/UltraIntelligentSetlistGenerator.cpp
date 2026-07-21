#include "UltraIntelligentSetlistGenerator.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <numeric>

namespace BeatMate::Services::Preparation {

GeneratedSetlist UltraIntelligentSetlistGenerator::generate(
    const std::vector<Models::Track>& pool, const SetlistConstraints& constraints) {
    return generateWithProgress(pool, constraints, nullptr);
}

GeneratedSetlist UltraIntelligentSetlistGenerator::generateWithProgress(
    const std::vector<Models::Track>& pool, const SetlistConstraints& constraints, ProgressCallback callback) {

    if (pool.empty()) return {};

    if (callback) callback(0.0f, "Analyzing track pool...");

    std::vector<Models::Track> candidates;
    for (const auto& t : pool) {
        if (t.bpm >= constraints.bpmRangeMin && t.bpm <= constraints.bpmRangeMax && t.duration > 0) {
            candidates.push_back(t);
        }
    }

    if (candidates.empty()) {
        candidates = pool;
    }

    double targetSeconds = constraints.durationMinutes * 60.0;
    double totalDuration = 0.0;
    std::vector<SetlistTrackScore> selectedScored;
    std::vector<Models::Track> selectedTracks;
    std::set<int64_t> usedIds;

    if (callback) callback(0.1f, "Selecting opening track...");

    float bestStartScore = -1.0f;
    size_t bestStartIdx = 0;
    for (size_t i = 0; i < candidates.size(); ++i) {
        float eDiff = std::abs(candidates[i].energy - constraints.energyArcStart);
        float score = 1.0f - eDiff / 10.0f;
        if (!constraints.preferredGenres.empty()) {
            score += genreMatchScore(candidates[i], constraints.preferredGenres) * 0.2f;
        }
        if (score > bestStartScore) {
            bestStartScore = score;
            bestStartIdx = i;
        }
    }

    SetlistTrackScore startScore;
    startScore.track = candidates[bestStartIdx];
    startScore.totalScore = bestStartScore;
    startScore.energyFitScore = 1.0f - std::abs(candidates[bestStartIdx].energy - constraints.energyArcStart) / 10.0f;
    selectedScored.push_back(startScore);
    selectedTracks.push_back(candidates[bestStartIdx]);
    usedIds.insert(candidates[bestStartIdx].id);
    totalDuration += candidates[bestStartIdx].duration;

    int maxTracks = static_cast<int>(candidates.size());
    int step = 0;

    while (totalDuration < targetSeconds && static_cast<int>(selectedTracks.size()) < maxTracks) {
        float position = static_cast<float>(totalDuration / targetSeconds);
        float targetEnergy = targetEnergyAtPosition(position, constraints);

        if (callback) {
            float progress = 0.1f + 0.8f * position;
            callback(progress, "Selecting track " + std::to_string(selectedTracks.size() + 1) + "...");
        }

        float bestScore = -999.0f;
        size_t bestIdx = 0;
        SetlistTrackScore bestTrackScore;

        for (size_t i = 0; i < candidates.size(); ++i) {
            if (usedIds.count(candidates[i].id)) continue;

            auto scored = scoreTrack(candidates[i], selectedTracks.back(), targetEnergy,
                                      position, constraints, selectedTracks);
            if (scored.totalScore > bestScore) {
                bestScore = scored.totalScore;
                bestIdx = i;
                bestTrackScore = scored;
            }
        }

        if (bestScore <= -999.0f) break;

        selectedScored.push_back(bestTrackScore);
        selectedTracks.push_back(candidates[bestIdx]);
        usedIds.insert(candidates[bestIdx].id);
        totalDuration += candidates[bestIdx].duration;
        ++step;
    }

    if (callback) callback(1.0f, "Setlist complete!");

    auto result = buildResult(selectedScored, constraints);
    spdlog::info("UltraIntelligentSetlistGenerator: Generated {} tracks, {:.1f} min, score={:.2f}",
                 result.trackCount, result.totalDuration / 60.0, result.totalScore);
    return result;
}

float UltraIntelligentSetlistGenerator::targetEnergyAtPosition(float position, const SetlistConstraints& constraints) const {
    float start = constraints.energyArcStart;
    float peak = constraints.energyArcPeak;
    float end = constraints.energyArcEnd;

    if (position < 0.4f) {
        float t = position / 0.4f;
        return start + (peak - start) * t * t;
    } else if (position < 0.75f) {
        float t = (position - 0.4f) / 0.35f;
        return peak - std::abs(std::sin(t * 3.14159f * 0.5f)) * 1.0f;
    } else {
        float t = (position - 0.75f) / 0.25f;
        return peak + (end - peak) * t;
    }
}

float UltraIntelligentSetlistGenerator::harmonicScoreBetween(const Models::Track& a, const Models::Track& b) const {
    std::string key1 = a.camelotKey.empty() ? a.key : a.camelotKey;
    std::string key2 = b.camelotKey.empty() ? b.key : b.camelotKey;
    if (key1.empty() || key2.empty()) return 0.5f;
    if (key1 == key2) return 1.0f;
    if (isKeyCompatible(key1, key2)) return 0.85f;
    return 0.0f;
}

float UltraIntelligentSetlistGenerator::bpmFlowScore(double prevBpm, double nextBpm) const {
    if (prevBpm <= 0 || nextBpm <= 0) return 0.5f;
    double diff = std::abs(prevBpm - nextBpm);
    if (diff <= 2.0) return 1.0f;
    if (diff <= 4.0) return 0.85f;
    if (diff <= 8.0) return 0.6f;
    double halfDiff = std::abs(prevBpm - nextBpm * 2.0);
    if (halfDiff <= 4.0) return 0.7f;
    return std::max(0.0f, static_cast<float>(1.0 - diff / 20.0));
}

float UltraIntelligentSetlistGenerator::genreMatchScore(const Models::Track& track, const std::vector<std::string>& preferredGenres) const {
    if (preferredGenres.empty()) return 0.5f;
    for (const auto& g : preferredGenres) {
        if (track.genre == g) return 1.0f;
    }
    return 0.2f;
}

float UltraIntelligentSetlistGenerator::diversityScore(const Models::Track& track, const std::vector<Models::Track>& selected) const {
    if (selected.empty()) return 1.0f;
    int sameArtist = 0;
    int sameGenre = 0;
    for (const auto& s : selected) {
        if (s.artist == track.artist) ++sameArtist;
        if (s.genre == track.genre) ++sameGenre;
    }
    float artistPenalty = sameArtist > 0 ? 0.5f / static_cast<float>(sameArtist + 1) : 1.0f;
    float genreRatio = static_cast<float>(sameGenre) / static_cast<float>(selected.size());
    float genrePenalty = genreRatio > 0.5f ? 0.7f : 1.0f;
    return artistPenalty * genrePenalty;
}

SetlistTrackScore UltraIntelligentSetlistGenerator::scoreTrack(
    const Models::Track& track, const Models::Track& prev, float targetEnergy, float position,
    const SetlistConstraints& constraints, const std::vector<Models::Track>& selected) {

    SetlistTrackScore scored;
    scored.track = track;
    scored.harmonicScore = harmonicScoreBetween(prev, track);
    scored.energyFitScore = 1.0f - std::abs(track.energy - targetEnergy) / 10.0f;
    scored.bpmFlowScore = bpmFlowScore(prev.bpm, track.bpm);
    scored.genreScore = genreMatchScore(track, constraints.preferredGenres);
    scored.diversityBonus = diversityScore(track, selected) * constraints.diversityFactor;

    scored.totalScore =
        scored.harmonicScore * 0.25f +
        scored.energyFitScore * (constraints.enforceEnergyArc ? 0.3f : 0.15f) +
        scored.bpmFlowScore * 0.25f +
        scored.genreScore * 0.1f +
        scored.diversityBonus * 0.1f;

    return scored;
}

bool UltraIntelligentSetlistGenerator::isKeyCompatible(const std::string& key1, const std::string& key2) const {
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

GeneratedSetlist UltraIntelligentSetlistGenerator::buildResult(
    const std::vector<SetlistTrackScore>& scored, const SetlistConstraints& constraints) {

    GeneratedSetlist result;
    result.scoredTracks = scored;
    result.trackCount = static_cast<int>(scored.size());
    result.totalDuration = 0.0;
    float harmonySum = 0.0f, energySum = 0.0f, coherenceSum = 0.0f;

    for (const auto& s : scored) {
        result.totalDuration += s.track.duration;
        harmonySum += s.harmonicScore;
        energySum += s.energyFitScore;
        coherenceSum += s.bpmFlowScore;
    }

    if (!scored.empty()) {
        result.overallHarmony = harmonySum / static_cast<float>(scored.size());
        result.overallEnergyFit = energySum / static_cast<float>(scored.size());
        result.overallCoherence = coherenceSum / static_cast<float>(scored.size());
    }
    result.totalScore = result.overallHarmony * 0.35f + result.overallEnergyFit * 0.35f + result.overallCoherence * 0.3f;

    result.summary = std::to_string(result.trackCount) + " tracks, " +
                     std::to_string(static_cast<int>(result.totalDuration / 60.0)) + " min, " +
                     "harmony=" + std::to_string(static_cast<int>(result.overallHarmony * 100)) + "%, " +
                     "energy=" + std::to_string(static_cast<int>(result.overallEnergyFit * 100)) + "%";
    return result;
}

}
