#include "PreparationIntegrationEngine.h"
#include "../library/TrackDatabase.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace BeatMate::Services::Suggestions {

PreparationIntegrationEngine::PreparationIntegrationEngine(std::shared_ptr<Library::TrackDatabase> database)
    : database_(std::move(database)) {}

std::vector<PreparationSuggestion> PreparationIntegrationEngine::suggestForSet(
    const Models::SetPlan& set, int count) {

    if (!database_ || set.entries.empty()) return {};
    auto allTracks = database_->getAllTracks();

    std::vector<PreparationSuggestion> suggestions;

    for (size_t i = 0; i < set.entries.size() - 1; ++i) {
        const auto& current = set.entries[i];
        const auto& next = set.entries[i + 1];

        double bpmDiff = std::abs(current.trackBpm - next.trackBpm);
        float energyDiff = std::abs(current.trackEnergy - next.trackEnergy);

        if (bpmDiff > 8.0 || energyDiff > 3.0f) {
            Models::Track before;
            before.bpm = current.trackBpm;
            before.camelotKey = current.trackKey;
            before.energy = current.trackEnergy;
            before.title = current.trackTitle;

            Models::Track after;
            after.bpm = next.trackBpm;
            after.camelotKey = next.trackKey;
            after.energy = next.trackEnergy;
            after.title = next.trackTitle;

            for (const auto& candidate : allTracks) {
                float score = gapFitScore(candidate, before, after);
                if (score > 0.5f) {
                    PreparationSuggestion sug;
                    sug.base.track = candidate;
                    sug.base.score = score;
                    sug.base.reason = "Bridge between '" + current.trackTitle + "' and '" + next.trackTitle + "'";
                    sug.context = "set_gap";
                    sug.suggestedPosition = static_cast<int>(i + 1);
                    sug.setImprovementScore = score;
                    suggestions.push_back(sug);
                }
            }
        }
    }

    std::sort(suggestions.begin(), suggestions.end(),
              [](const auto& a, const auto& b) { return a.setImprovementScore > b.setImprovementScore; });
    if (static_cast<int>(suggestions.size()) > count) suggestions.resize(static_cast<size_t>(count));

    spdlog::info("PreparationIntegrationEngine: {} suggestions to improve set", suggestions.size());
    return suggestions;
}

std::vector<PreparationSuggestion> PreparationIntegrationEngine::suggestForGap(
    const Models::Track& before, const Models::Track& after, int count) {

    if (!database_) return {};
    auto allTracks = database_->getAllTracks();
    std::vector<PreparationSuggestion> results;

    for (const auto& candidate : allTracks) {
        if (candidate.id == before.id || candidate.id == after.id) continue;

        float score = gapFitScore(candidate, before, after);
        if (score > 0.4f) {
            PreparationSuggestion sug;
            sug.base.track = candidate;
            sug.base.score = score;
            sug.base.reason = "Bridge track";
            sug.context = "set_gap";
            sug.setImprovementScore = score;
            results.push_back(sug);
        }
    }

    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) { return a.base.score > b.base.score; });
    if (static_cast<int>(results.size()) > count) results.resize(static_cast<size_t>(count));
    return results;
}

std::vector<PreparationSuggestion> PreparationIntegrationEngine::suggestForEvent(
    const Models::EventPlan& event, const std::string& sectionName, int count) {

    if (!database_) return {};
    auto allTracks = database_->getAllTracks();
    std::vector<PreparationSuggestion> results;

    const Models::EventSection* targetSection = nullptr;
    for (const auto& section : event.sections) {
        if (section.name == sectionName) { targetSection = &section; break; }
    }
    if (!targetSection) return {};

    for (const auto& candidate : allTracks) {
        float score = 0.0f;

        float energyDiff = std::abs(candidate.energy - targetSection->energy);
        score += std::max(0.0f, 1.0f - energyDiff / 5.0f) * 0.4f;

        if (targetSection->bpmMin > 0 && targetSection->bpmMax > 0) {
            if (candidate.bpm >= targetSection->bpmMin && candidate.bpm <= targetSection->bpmMax)
                score += 0.3f;
        } else {
            score += 0.15f;
        }

        if (!targetSection->genre.empty() && candidate.genre == targetSection->genre)
            score += 0.3f;

        if (score > 0.3f) {
            PreparationSuggestion sug;
            sug.base.track = candidate;
            sug.base.score = score;
            sug.base.reason = "Fits section '" + sectionName + "'";
            sug.context = "event_section";
            sug.setImprovementScore = score;
            results.push_back(sug);
        }
    }

    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) { return a.base.score > b.base.score; });
    if (static_cast<int>(results.size()) > count) results.resize(static_cast<size_t>(count));

    spdlog::info("PreparationIntegrationEngine: {} suggestions for event section '{}'", results.size(), sectionName);
    return results;
}

std::vector<PreparationSuggestion> PreparationIntegrationEngine::suggestToImproveEnergy(
    const Models::SetPlan& set, float targetEnergy, int position, int count) {

    if (!database_) return {};
    auto allTracks = database_->getAllTracks();
    std::vector<PreparationSuggestion> results;

    for (const auto& candidate : allTracks) {
        float energyDiff = std::abs(candidate.energy - targetEnergy);
        float score = std::max(0.0f, 1.0f - energyDiff / 5.0f);
        float improvement = setImprovementScore(candidate, set, position);
        score = score * 0.6f + improvement * 0.4f;

        if (score > 0.4f) {
            PreparationSuggestion sug;
            sug.base.track = candidate;
            sug.base.score = score;
            sug.base.reason = "Energy fill at position " + std::to_string(position);
            sug.context = "energy_fill";
            sug.suggestedPosition = position;
            sug.setImprovementScore = improvement;
            results.push_back(sug);
        }
    }

    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) { return a.base.score > b.base.score; });
    if (static_cast<int>(results.size()) > count) results.resize(static_cast<size_t>(count));
    return results;
}

float PreparationIntegrationEngine::gapFitScore(
    const Models::Track& candidate, const Models::Track& before, const Models::Track& after) const {

    float bpmFit = 0.0f;
    if (before.bpm > 0 && after.bpm > 0 && candidate.bpm > 0) {
        double midBpm = (before.bpm + after.bpm) / 2.0;
        bpmFit = std::max(0.0f, static_cast<float>(1.0 - std::abs(candidate.bpm - midBpm) / 10.0));
    }

    float midEnergy = (before.energy + after.energy) / 2.0f;
    float energyFit = std::max(0.0f, 1.0f - std::abs(candidate.energy - midEnergy) / 5.0f);

    std::string candKey = candidate.camelotKey.empty() ? candidate.key : candidate.camelotKey;
    std::string beforeKey = before.camelotKey.empty() ? before.key : before.camelotKey;
    std::string afterKey = after.camelotKey.empty() ? after.key : after.camelotKey;

    float keyFit = 0.0f;
    bool compatBefore = candKey == beforeKey || isKeyCompatible(candKey, beforeKey);
    bool compatAfter = candKey == afterKey || isKeyCompatible(candKey, afterKey);
    if (compatBefore && compatAfter) keyFit = 1.0f;
    else if (compatBefore || compatAfter) keyFit = 0.6f;

    return bpmFit * 0.3f + energyFit * 0.3f + keyFit * 0.4f;
}

float PreparationIntegrationEngine::setImprovementScore(
    const Models::Track& candidate, const Models::SetPlan& set, int position) const {

    if (set.entries.empty() || position < 0) return 0.5f;

    float score = 0.5f;
    int pos = std::min(position, static_cast<int>(set.entries.size()) - 1);

    if (pos > 0) {
        const auto& prev = set.entries[static_cast<size_t>(pos - 1)];
        float bpmDiff = static_cast<float>(std::abs(candidate.bpm - prev.trackBpm));
        if (bpmDiff <= 4.0f) score += 0.25f;
    }

    if (pos < static_cast<int>(set.entries.size()) - 1) {
        const auto& next = set.entries[static_cast<size_t>(pos + 1)];
        float bpmDiff = static_cast<float>(std::abs(candidate.bpm - next.trackBpm));
        if (bpmDiff <= 4.0f) score += 0.25f;
    }

    return std::min(1.0f, score);
}

bool PreparationIntegrationEngine::isKeyCompatible(const std::string& key1, const std::string& key2) const {
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
