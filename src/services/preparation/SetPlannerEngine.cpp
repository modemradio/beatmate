#include "SetPlannerEngine.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <random>
#include <numeric>

namespace BeatMate::Services::Preparation {

PlannerResult SetPlannerEngine::planOptimal(const std::vector<Models::Track>& tracks, const PlannerConfig& config) {
    if (tracks.size() <= 10) {
        return planSimulatedAnnealing(tracks, config);
    }
    return planNearestNeighbor(tracks, config);
}

PlannerResult SetPlannerEngine::planNearestNeighbor(const std::vector<Models::Track>& tracks, const PlannerConfig& config) {
    if (tracks.empty()) return {};

    std::vector<bool> used(tracks.size(), false);
    std::vector<Models::Track> ordered;
    ordered.reserve(tracks.size());

    size_t startIdx = 0;
    double minBpm = 999.0;
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i].bpm > 0 && tracks[i].bpm < minBpm) {
            minBpm = tracks[i].bpm;
            startIdx = i;
        }
    }

    ordered.push_back(tracks[startIdx]);
    used[startIdx] = true;

    for (size_t step = 1; step < tracks.size(); ++step) {
        float bestScore = -1.0f;
        size_t bestIdx = 0;

        for (size_t i = 0; i < tracks.size(); ++i) {
            if (used[i]) continue;
            float score = transitionScore(ordered.back(), tracks[i], config);
            if (score > bestScore) {
                bestScore = score;
                bestIdx = i;
            }
        }

        ordered.push_back(tracks[bestIdx]);
        used[bestIdx] = true;
    }

    auto result = buildResult(ordered, config, 0);
    spdlog::info("SetPlannerEngine: Nearest-neighbor plan - {} tracks, score={:.3f}", ordered.size(), result.totalScore);
    return result;
}

PlannerResult SetPlannerEngine::planOptimal2Opt(const std::vector<Models::Track>& tracks, const PlannerConfig& config, int maxIterations) {
    if (tracks.size() < 2) return buildResult(tracks, config, 0);

    // WHY: NN seed for larger inputs gives 2-opt a better starting basin than raw input order.
    std::vector<Models::Track> current;
    if (tracks.size() > 20) {
        current = planNearestNeighbor(tracks, config).orderedTracks;
    } else {
        current = tracks;
    }

    float currentScore = calculatePathScore(current, config);
    int iterations = 0;
    bool improved = true;

    while (improved && iterations < maxIterations) {
        improved = false;
        for (size_t i = 0; i < current.size() - 1 && iterations < maxIterations; ++i) {
            for (size_t j = i + 1; j < current.size() && iterations < maxIterations; ++j) {
                ++iterations;
                std::reverse(current.begin() + static_cast<long>(i), current.begin() + static_cast<long>(j) + 1);
                float newScore = calculatePathScore(current, config);
                if (newScore > currentScore) {
                    currentScore = newScore;
                    improved = true;
                } else {
                    // WHY: revert non-improving reversal to keep pure 2-opt (no acceptance of worse moves).
                    std::reverse(current.begin() + static_cast<long>(i), current.begin() + static_cast<long>(j) + 1);
                }
            }
        }
    }

    auto result = buildResult(current, config, iterations);
    spdlog::info("SetPlannerEngine: 2-opt plan - {} tracks, score={:.3f}, iterations={}",
                 current.size(), result.totalScore, iterations);
    return result;
}

PlannerResult SetPlannerEngine::planSimulatedAnnealing(const std::vector<Models::Track>& tracks, const PlannerConfig& config) {
    if (tracks.size() < 2) return buildResult(tracks, config, 0);

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> uni01(0.0f, 1.0f);
    std::vector<Models::Track> current = tracks;
    float currentScore = calculatePathScore(current, config);
    std::vector<Models::Track> best = current;
    float bestScore = currentScore;

    float temperature = config.temperatureStart;
    int iterations = 0;

    for (int i = 0; i < config.maxIterations; ++i) {
        std::uniform_int_distribution<size_t> idxDist(0, current.size() - 1);
        size_t a = idxDist(rng);
        size_t b = idxDist(rng);
        if (a == b) continue;
        if (a > b) std::swap(a, b);

        std::reverse(current.begin() + static_cast<long>(a), current.begin() + static_cast<long>(b) + 1);

        float newScore = calculatePathScore(current, config);
        float delta = newScore - currentScore;

        bool accept = (delta > 0.0f) ||
                      (temperature > 0.0f && uni01(rng) < std::exp(delta / temperature));

        if (accept) {
            currentScore = newScore;
            if (currentScore > bestScore) {
                bestScore = currentScore;
                best = current;
            }
        } else {
            std::reverse(current.begin() + static_cast<long>(a), current.begin() + static_cast<long>(b) + 1);
        }

        temperature *= config.temperatureCooling;
        ++iterations;
    }

    auto result = buildResult(best, config, iterations);
    spdlog::info("SetPlannerEngine: SA plan - {} tracks, score={:.3f}, iterations={}",
                 best.size(), result.totalScore, iterations);
    return result;
}

float SetPlannerEngine::calculatePathScore(const std::vector<Models::Track>& order, const PlannerConfig& config) const {
    if (order.size() < 2) return 0.0f;
    float total = 0.0f;
    for (size_t i = 0; i < order.size() - 1; ++i) {
        total += transitionScore(order[i], order[i + 1], config);
    }
    return total / static_cast<float>(order.size() - 1);
}

float SetPlannerEngine::transitionScore(const Models::Track& from, const Models::Track& to, const PlannerConfig& config) const {
    float bs = bpmScore(from.bpm, to.bpm);
    float ks = keyScore(from.camelotKey.empty() ? from.key : from.camelotKey,
                        to.camelotKey.empty() ? to.key : to.camelotKey);
    float es = energyScore(from.energy, to.energy);
    float gs = genreScore(from.genre, to.genre);
    return bs * config.bpmWeight + ks * config.keyWeight + es * config.energyWeight + gs * config.genreWeight;
}

float SetPlannerEngine::bpmScore(double bpm1, double bpm2) const {
    if (bpm1 <= 0 || bpm2 <= 0) return 0.5f;
    double diff = std::abs(bpm1 - bpm2);
    double maxDiff = bpm1 * 0.06;
    if (diff <= maxDiff) return static_cast<float>(1.0 - diff / maxDiff);
    double diffHalf = std::abs(bpm1 - bpm2 * 2.0);
    if (diffHalf <= maxDiff) return static_cast<float>(0.7 * (1.0 - diffHalf / maxDiff));
    return 0.0f;
}

float SetPlannerEngine::keyScore(const std::string& key1, const std::string& key2) const {
    // Same Camelot rules as SetCompatibilityScorer::scoreKey.
    if (key1.empty() || key2.empty()) return 0.5f;
    if (key1.size() < 2 || key2.size() < 2) return 0.0f;
    try {
        int num1 = std::stoi(key1.substr(0, key1.size() - 1));
        int num2 = std::stoi(key2.substr(0, key2.size() - 1));
        char let1 = static_cast<char>(std::toupper(static_cast<unsigned char>(key1.back())));
        char let2 = static_cast<char>(std::toupper(static_cast<unsigned char>(key2.back())));

        if (num1 == num2 && let1 == let2) return 1.0f;             // same key
        int diff = std::abs(num1 - num2);
        if (diff == 11) diff = 1;                                  // wrap around 12->1
        if (diff == 1 && let1 == let2) return 25.0f / 30.0f;       // +/-1 same letter
        if (num1 == num2 && let1 != let2) return 22.0f / 30.0f;    // relative major/minor
        if (diff == 2 && let1 == let2) return 18.0f / 30.0f;       // +2 semitone (mood shift)
        if ((diff == 7 || diff == 5) && let1 == let2) return 15.0f / 30.0f; // dominant
        if (diff == 1 && let1 != let2) return 12.0f / 30.0f;       // diagonal
        return std::max(0.0f, (8.0f - static_cast<float>(diff)) / 30.0f);   // far
    } catch (...) {}
    return 0.0f;
}

float SetPlannerEngine::energyScore(float e1, float e2) const {
    float diff = std::abs(e1 - e2);
    if (diff <= 2.0f) return 1.0f - diff / 4.0f;
    return std::max(0.0f, 1.0f - diff / 10.0f);
}

float SetPlannerEngine::genreScore(const std::string& g1, const std::string& g2) const {
    if (g1.empty() || g2.empty()) return 0.5f;
    return (g1 == g2) ? 1.0f : 0.2f;
}

PlannerResult SetPlannerEngine::buildResult(const std::vector<Models::Track>& order, const PlannerConfig& config, int iterations) {
    PlannerResult result;
    result.orderedTracks = order;
    result.iterations = iterations;
    result.totalScore = calculatePathScore(order, config);
    result.avgTransitionScore = result.totalScore;
    result.totalDuration = 0.0;
    for (const auto& t : order) result.totalDuration += t.duration;
    return result;
}

} // namespace BeatMate::Services::Preparation
