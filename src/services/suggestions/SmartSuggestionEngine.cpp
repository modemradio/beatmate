#include "SmartSuggestionEngine.h"
#include "../library/TrackDatabase.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>

namespace BeatMate::Services::Suggestions {

SmartSuggestionEngine::SmartSuggestionEngine(std::shared_ptr<Library::TrackDatabase> database)
    : database_(std::move(database)) {}

std::vector<RecommendationResult> SmartSuggestionEngine::suggest(const Models::Track& current, int count) {
    if (!database_) {
        spdlog::debug("SmartSuggestionEngine: No database, skipping suggest");
        return {};
    }
    auto allTracks = database_->getAllTracks();
    std::vector<RecommendationResult> results;

    for (const auto& candidate : allTracks) {
        if (candidate.id == current.id) continue;

        float score = calculateScore(current, candidate);
        if (score > 0.3f) {
            RecommendationResult result;
            result.track = candidate;
            result.score = score;
            result.componentScores["bpm"] = isBpmCompatible(current.bpm, candidate.bpm) ? 1.0f : 0.0f;
            result.componentScores["key"] = isKeyCompatible(current.camelotKey, candidate.camelotKey) ? 1.0f : 0.0f;
            result.componentScores["energy"] = 1.0f - std::min(1.0f, std::abs(current.energy - candidate.energy) / 10.0f);
            result.reason = "BPM: " + std::to_string(static_cast<int>(candidate.bpm)) +
                           ", Key: " + candidate.camelotKey;
            results.push_back(result);
        }
    }

    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    if (static_cast<int>(results.size()) > count) results.resize(static_cast<size_t>(count));
    return results;
}

bool SmartSuggestionEngine::isBpmCompatible(double bpm1, double bpm2) const {
    if (bpm1 <= 0 || bpm2 <= 0) return false;
    double diff = std::abs(bpm1 - bpm2) / bpm1 * 100.0;
    double diffHalf = std::abs(bpm1 - bpm2 * 2.0) / bpm1 * 100.0;
    double diffDouble = std::abs(bpm1 * 2.0 - bpm2) / (bpm1 * 2.0) * 100.0;
    return diff <= bpmTolerance_ || diffHalf <= bpmTolerance_ || diffDouble <= bpmTolerance_;
}

bool SmartSuggestionEngine::isKeyCompatible(const std::string& key1, const std::string& key2) const {
    if (key1.empty() || key2.empty()) return false;
    if (key1 == key2) return true;

    auto parseCamelot = [](const std::string& key) -> std::pair<int, char> {
        if (key.size() < 2) return {0, 'A'};
        try {
            int num = std::stoi(key.substr(0, key.size() - 1));
            char letter = key.back();
            return {num, letter};
        } catch (...) { return {0, 'A'}; }
    };

    auto [num1, let1] = parseCamelot(key1);
    auto [num2, let2] = parseCamelot(key2);

    if (num1 == 0 || num2 == 0) return false;

    if (num1 == num2 && let1 == let2) return true;
    int diff = ((num2 - num1) + 12) % 12;
    if ((diff == 1 || diff == 11) && let1 == let2) return true;
    if (num1 == num2 && let1 != let2) return true;

    return false;
}

float SmartSuggestionEngine::calculateScore(const Models::Track& current, const Models::Track& candidate) const {
    float score = 0.0f;
    float bpmScore = 0.0f, keyScore = 0.0f, energyScore = 0.0f, genreScore = 0.0f;

    if (isBpmCompatible(current.bpm, candidate.bpm)) {
        double diff = std::abs(current.bpm - candidate.bpm) / current.bpm * 100.0;
        bpmScore = 1.0f - static_cast<float>(diff / bpmTolerance_);
    }

    if (isKeyCompatible(current.camelotKey, candidate.camelotKey)) {
        keyScore = (current.camelotKey == candidate.camelotKey) ? 1.0f : 0.8f;
    }

    energyScore = 1.0f - std::min(1.0f, std::abs(current.energy - candidate.energy) / energyTolerance_);

    if (!current.genre.empty() && current.genre == candidate.genre) {
        genreScore = 1.0f;
    }

    score = bpmScore * 0.3f + keyScore * 0.3f + energyScore * 0.2f + genreScore * 0.2f;
    return score;
}

} // namespace BeatMate::Services::Suggestions
