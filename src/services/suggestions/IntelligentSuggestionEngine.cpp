#include "IntelligentSuggestionEngine.h"
#include "../library/TrackDatabase.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <set>

namespace BeatMate::Services::Suggestions {

IntelligentSuggestionEngine::IntelligentSuggestionEngine(std::shared_ptr<Library::TrackDatabase> database)
    : database_(std::move(database)) {}

std::vector<IntelligentSuggestion> IntelligentSuggestionEngine::suggest(const Models::Track& current, int count) {
    return suggestWithContext(current, context_, count);
}

std::vector<IntelligentSuggestion> IntelligentSuggestionEngine::suggestWithContext(
    const Models::Track& current, const IntelligentContext& context, int count) {

    if (!database_) return {};
    auto allTracks = database_->getAllTracks();
    std::set<int64_t> recentSet(context.recentlyPlayed.begin(), context.recentlyPlayed.end());

    std::vector<IntelligentSuggestion> suggestions;

    for (const auto& candidate : allTracks) {
        if (candidate.id == current.id) continue;
        if (recentSet.count(candidate.id)) continue;

        IntelligentSuggestion sug;
        sug.track = candidate;
        sug.harmonicScore = harmonicScore(current, candidate);
        sug.energyFlowScore = energyFlowScore(current, candidate);
        sug.contextScore = contextualScore(candidate, context);
        sug.noveltyScore = noveltyScore(candidate, context);

        sug.score = sug.harmonicScore * 0.3f + sug.energyFlowScore * 0.25f +
                    sug.contextScore * 0.25f + sug.noveltyScore * 0.2f;

        if (sug.harmonicScore > 0.7f) sug.allReasons.push_back("Harmonic match");
        if (sug.energyFlowScore > 0.7f) sug.allReasons.push_back("Good energy flow");
        if (sug.contextScore > 0.7f) sug.allReasons.push_back("Fits context");
        if (sug.noveltyScore > 0.7f) sug.allReasons.push_back("Fresh pick");
        sug.primaryReason = sug.allReasons.empty() ? "Compatible" : sug.allReasons[0];

        if (sug.score > 0.25f) {
            suggestions.push_back(sug);
        }
    }

    std::sort(suggestions.begin(), suggestions.end(),
              [](const auto& a, const auto& b) { return a.score > b.score; });
    if (static_cast<int>(suggestions.size()) > count) suggestions.resize(static_cast<size_t>(count));

    spdlog::info("IntelligentSuggestionEngine: {} suggestions for '{}'", suggestions.size(), current.title);
    return suggestions;
}

float IntelligentSuggestionEngine::harmonicScore(const Models::Track& a, const Models::Track& b) const {
    std::string key1 = a.camelotKey.empty() ? a.key : a.camelotKey;
    std::string key2 = b.camelotKey.empty() ? b.key : b.camelotKey;
    if (key1.empty() || key2.empty()) return 0.5f;
    if (key1 == key2) return 1.0f;
    if (isKeyCompatible(key1, key2)) return 0.85f;

    float bpmFactor = 0.0f;
    if (a.bpm > 0 && b.bpm > 0) {
        double diff = std::abs(a.bpm - b.bpm);
        bpmFactor = std::max(0.0f, static_cast<float>(1.0 - diff / 10.0)) * 0.3f;
    }
    return bpmFactor;
}

float IntelligentSuggestionEngine::energyFlowScore(const Models::Track& current, const Models::Track& candidate) const {
    float diff = candidate.energy - current.energy;
    float absDiff = std::abs(diff);

    if (absDiff <= 1.0f) return 1.0f;
    if (absDiff <= 2.0f) return 0.8f;
    if (absDiff <= 3.0f) return 0.5f;

    if (context_.energyDirection > 0 && diff > 0) return 0.7f;
    if (context_.energyDirection < 0 && diff < 0) return 0.7f;

    return std::max(0.0f, 1.0f - absDiff / 10.0f);
}

float IntelligentSuggestionEngine::contextualScore(const Models::Track& candidate, const IntelligentContext& context) const {
    float score = 0.5f;

    if (!context.preferredGenre.empty()) {
        score += (candidate.genre == context.preferredGenre) ? 0.3f : 0.0f;
    }

    float adjustedEnergy = candidate.energy * context.timeOfDayFactor;
    float targetEnergy = 5.0f * context.crowdEnergyFactor;
    float energyFit = 1.0f - std::abs(adjustedEnergy - targetEnergy) / 10.0f;
    score += energyFit * 0.2f;

    return std::min(1.0f, score);
}

float IntelligentSuggestionEngine::noveltyScore(const Models::Track& candidate, const IntelligentContext& context) const {
    for (const auto& id : context.recentlyPlayed) {
        if (candidate.id == id) return 0.0f;
    }

    float playCountPenalty = std::min(1.0f, static_cast<float>(candidate.playCount) / 100.0f);
    return 1.0f - playCountPenalty * 0.3f;
}

bool IntelligentSuggestionEngine::isKeyCompatible(const std::string& key1, const std::string& key2) const {
    if (key1.size() < 2 || key2.size() < 2) return false;
    try {
        int num1 = std::stoi(key1.substr(0, key1.size() - 1));
        int num2 = std::stoi(key2.substr(0, key2.size() - 1));
        char let1 = key1.back(); char let2 = key2.back();
        if (num1 == num2 && let1 != let2) return true;
        int diff = ((num2 - num1) + 12) % 12;
        if ((diff == 1 || diff == 11) && let1 == let2) return true;
    } catch (...) {}
    return false;
}

} // namespace BeatMate::Services::Suggestions
