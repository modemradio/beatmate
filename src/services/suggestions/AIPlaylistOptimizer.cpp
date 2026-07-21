#include "AIPlaylistOptimizer.h"
#include "../library/TrackDatabase.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <random>
#include <numeric>
#include <set>

namespace BeatMate::Services::Suggestions {

AIPlaylistOptimizer::AIPlaylistOptimizer(std::shared_ptr<Library::TrackDatabase> database)
    : database_(std::move(database)) {}

OptimizationResult AIPlaylistOptimizer::optimize(const std::vector<Models::Track>& playlist, const OptimizationGoal& goal) {
    return optimizeWithProgress(playlist, goal, nullptr);
}

OptimizationResult AIPlaylistOptimizer::optimizeWithProgress(
    const std::vector<Models::Track>& playlist, const OptimizationGoal& goal, OptimizationProgressCallback callback) {

    if (playlist.size() < 2) {
        OptimizationResult r;
        r.optimizedPlaylist = playlist;
        r.scoreBefore = r.scoreAfter = evaluatePlaylist(playlist, goal);
        return r;
    }

    if (callback) callback(0.0f, "Evaluating current playlist...");
    float scoreBefore = evaluatePlaylist(playlist, goal);

    if (callback) callback(0.2f, "Reordering for optimal flow...");
    auto reorderResult = reorder(playlist, goal);

    if (callback) callback(0.6f, "Filling gaps...");
    auto fillResult = fillGaps(reorderResult.optimizedPlaylist, goal);

    if (callback) callback(0.9f, "Final evaluation...");
    float scoreAfter = evaluatePlaylist(fillResult.optimizedPlaylist, goal);

    OptimizationResult result;
    result.optimizedPlaylist = fillResult.optimizedPlaylist;
    result.scoreBefore = scoreBefore;
    result.scoreAfter = scoreAfter;
    result.improvement = scoreAfter - scoreBefore;
    result.swapsMade = reorderResult.swapsMade;
    result.tracksAdded = fillResult.tracksAdded;
    result.tracksRemoved = fillResult.tracksRemoved;

    result.summary = "Optimized: " + std::to_string(static_cast<int>(scoreBefore * 100)) + "% -> " +
                     std::to_string(static_cast<int>(scoreAfter * 100)) + "% (" +
                     std::to_string(result.swapsMade) + " swaps, " +
                     std::to_string(result.tracksAdded) + " added)";

    if (callback) callback(1.0f, "Optimization complete");
    spdlog::info("AIPlaylistOptimizer: {}", result.summary);
    return result;
}

OptimizationResult AIPlaylistOptimizer::reorder(const std::vector<Models::Track>& playlist, const OptimizationGoal& goal) {
    OptimizationResult result;
    auto current = playlist;
    float currentScore = evaluatePlaylist(current, goal);
    auto best = current;
    float bestScore = currentScore;
    int swaps = 0;

    std::mt19937 rng(42);
    float temperature = 50.0f;
    int iterations = static_cast<int>(playlist.size()) * 100;

    for (int i = 0; i < iterations; ++i) {
        std::uniform_int_distribution<size_t> dist(0, current.size() - 1);
        size_t a = dist(rng);
        size_t b = dist(rng);
        if (a == b) continue;

        std::swap(current[a], current[b]);
        float newScore = evaluatePlaylist(current, goal);

        float delta = newScore - currentScore;
        if (delta > 0 || std::exp(delta * 100.0f / temperature) > static_cast<float>(dist(rng)) / static_cast<float>(current.size())) {
            currentScore = newScore;
            ++swaps;
            if (currentScore > bestScore) {
                bestScore = currentScore;
                best = current;
            }
        } else {
            std::swap(current[a], current[b]); // revert
        }

        temperature *= 0.998f;
    }

    result.optimizedPlaylist = best;
    result.scoreBefore = evaluatePlaylist(playlist, goal);
    result.scoreAfter = bestScore;
    result.swapsMade = swaps;
    return result;
}

OptimizationResult AIPlaylistOptimizer::fillGaps(const std::vector<Models::Track>& playlist, const OptimizationGoal& goal) {
    OptimizationResult result;
    result.optimizedPlaylist = playlist;
    result.tracksAdded = 0;
    result.tracksRemoved = 0;

    if (!database_ || playlist.size() < 2) return result;

    auto pool = database_->getAllTracks();
    std::set<int64_t> usedIds;
    for (const auto& t : playlist) usedIds.insert(t.id);

    std::vector<Models::Track> improved;
    for (size_t i = 0; i < playlist.size(); ++i) {
        improved.push_back(playlist[i]);

        if (i < playlist.size() - 1) {
            double bpmDiff = std::abs(playlist[i].bpm - playlist[i + 1].bpm);
            float energyDiff = std::abs(playlist[i].energy - playlist[i + 1].energy);

            if (bpmDiff > 10.0 || energyDiff > 4.0f) {
                float bestScore = -1.0f;
                Models::Track bestBridge;
                bool found = false;

                for (const auto& candidate : pool) {
                    if (usedIds.count(candidate.id)) continue;

                    double midBpm = (playlist[i].bpm + playlist[i + 1].bpm) / 2.0;
                    float midEnergy = (playlist[i].energy + playlist[i + 1].energy) / 2.0f;

                    float bpmFit = std::max(0.0f, static_cast<float>(1.0 - std::abs(candidate.bpm - midBpm) / 10.0));
                    float energyFit = std::max(0.0f, 1.0f - std::abs(candidate.energy - midEnergy) / 5.0f);
                    float score = bpmFit * 0.5f + energyFit * 0.5f;

                    if (score > bestScore && score > 0.5f) {
                        bestScore = score;
                        bestBridge = candidate;
                        found = true;
                    }
                }

                if (found) {
                    improved.push_back(bestBridge);
                    usedIds.insert(bestBridge.id);
                    ++result.tracksAdded;
                }
            }
        }
    }

    result.optimizedPlaylist = improved;
    return result;
}

float AIPlaylistOptimizer::evaluatePlaylist(const std::vector<Models::Track>& playlist, const OptimizationGoal& goal) const {
    if (playlist.size() < 2) return 0.5f;

    float bpm = bpmFlowScore(playlist) * goal.smoothBpmWeight;
    float harmonic = harmonicScore(playlist) * goal.harmonicMixWeight;
    float energy = energyArcScore(playlist, goal) * goal.energyArcWeight;
    float genre = genreCohesionScore(playlist) * goal.genreCohesionWeight;

    return bpm + harmonic + energy + genre;
}

float AIPlaylistOptimizer::bpmFlowScore(const std::vector<Models::Track>& playlist) const {
    if (playlist.size() < 2) return 1.0f;
    float totalScore = 0.0f;
    for (size_t i = 1; i < playlist.size(); ++i) {
        double diff = std::abs(playlist[i].bpm - playlist[i - 1].bpm);
        totalScore += std::max(0.0f, static_cast<float>(1.0 - diff / 10.0));
    }
    return totalScore / static_cast<float>(playlist.size() - 1);
}

float AIPlaylistOptimizer::harmonicScore(const std::vector<Models::Track>& playlist) const {
    if (playlist.size() < 2) return 1.0f;
    float totalScore = 0.0f;
    for (size_t i = 1; i < playlist.size(); ++i) {
        std::string key1 = playlist[i - 1].camelotKey.empty() ? playlist[i - 1].key : playlist[i - 1].camelotKey;
        std::string key2 = playlist[i].camelotKey.empty() ? playlist[i].key : playlist[i].camelotKey;
        if (key1 == key2) totalScore += 1.0f;
        else if (isKeyCompatible(key1, key2)) totalScore += 0.85f;
        else totalScore += 0.1f;
    }
    return totalScore / static_cast<float>(playlist.size() - 1);
}

float AIPlaylistOptimizer::energyArcScore(const std::vector<Models::Track>& playlist, const OptimizationGoal& goal) const {
    if (playlist.empty()) return 0.0f;
    float totalScore = 0.0f;
    for (size_t i = 0; i < playlist.size(); ++i) {
        float position = static_cast<float>(i) / static_cast<float>(playlist.size() - 1);
        float target = targetEnergyAt(position, goal);
        float diff = std::abs(playlist[i].energy - target);
        totalScore += std::max(0.0f, 1.0f - diff / 5.0f);
    }
    return totalScore / static_cast<float>(playlist.size());
}

float AIPlaylistOptimizer::genreCohesionScore(const std::vector<Models::Track>& playlist) const {
    if (playlist.size() < 2) return 1.0f;
    float sameGenre = 0.0f;
    for (size_t i = 1; i < playlist.size(); ++i) {
        if (!playlist[i].genre.empty() && playlist[i].genre == playlist[i - 1].genre) sameGenre += 1.0f;
        else sameGenre += 0.3f;
    }
    return sameGenre / static_cast<float>(playlist.size() - 1);
}

float AIPlaylistOptimizer::targetEnergyAt(float position, const OptimizationGoal& goal) const {
    if (position < 0.4f) {
        float t = position / 0.4f;
        return goal.targetEnergyStart + (goal.targetEnergyPeak - goal.targetEnergyStart) * t;
    } else if (position < 0.75f) {
        return goal.targetEnergyPeak;
    } else {
        float t = (position - 0.75f) / 0.25f;
        return goal.targetEnergyPeak + (goal.targetEnergyEnd - goal.targetEnergyPeak) * t;
    }
}

bool AIPlaylistOptimizer::isKeyCompatible(const std::string& key1, const std::string& key2) const {
    if (key1.size() < 2 || key2.size() < 2) return false;
    try {
        int num1 = std::stoi(key1.substr(0, key1.size() - 1));
        int num2 = std::stoi(key2.substr(0, key2.size() - 1));
        char let1 = key1.back(); char let2 = key2.back();
        if (num1 == num2 && let1 != let2) return true;
        int diff = ((num2 - num1) + 12) % 12;
        return (diff == 1 || diff == 11) && let1 == let2;
    } catch (...) {}
    return false;
}

} // namespace BeatMate::Services::Suggestions
