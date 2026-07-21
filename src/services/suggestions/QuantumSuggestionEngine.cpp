#include "QuantumSuggestionEngine.h"
#include "../library/TrackDatabase.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <set>

namespace BeatMate::Services::Suggestions {

QuantumSuggestionEngine::QuantumSuggestionEngine(std::shared_ptr<Library::TrackDatabase> database)
    : database_(std::move(database)) {}

std::vector<QuantumSuggestion> QuantumSuggestionEngine::suggest(const Models::Track& current, int count) {
    return superpositionAnalysis(current, {}, count);
}

std::vector<QuantumSuggestion> QuantumSuggestionEngine::superpositionAnalysis(
    const Models::Track& current, const std::vector<Models::Track>& history, int count) {

    if (!database_) return {};
    auto allTracks = database_->getAllTracks();
    std::set<int64_t> historyIds;
    for (const auto& h : history) historyIds.insert(h.id);

    std::vector<QuantumSuggestion> results;

    for (const auto& candidate : allTracks) {
        if (candidate.id == current.id) continue;
        if (historyIds.count(candidate.id)) continue;

        QuantumSuggestion sug;
        sug.track = candidate;
        sug.state = computeQuantumState(current, candidate, history);
        sug.state.collapsedScore = collapseState(sug.state);

        sug.confidence = (sug.state.harmonicProbability + sug.state.rhythmicProbability) / 2.0f;

        sug.probabilityMap = {
            {"Harmonic", sug.state.harmonicProbability},
            {"Energy", sug.state.energyProbability},
            {"Rhythmic", sug.state.rhythmicProbability},
            {"Cultural", sug.state.culturalProbability}
        };

        if (sug.state.collapsedScore > 0.8f) sug.quantumInsight = "Strong quantum resonance - highly recommended";
        else if (sug.state.collapsedScore > 0.6f) sug.quantumInsight = "Moderate quantum entanglement - good choice";
        else if (sug.state.collapsedScore > 0.4f) sug.quantumInsight = "Weak quantum coupling - acceptable";
        else sug.quantumInsight = "Low quantum coherence - risky transition";

        if (sug.state.collapsedScore > 0.2f) results.push_back(sug);
    }

    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) { return a.state.collapsedScore > b.state.collapsedScore; });
    if (static_cast<int>(results.size()) > count) results.resize(static_cast<size_t>(count));

    spdlog::info("QuantumSuggestionEngine: {} quantum suggestions for '{}'", results.size(), current.title);
    return results;
}

QuantumState QuantumSuggestionEngine::computeQuantumState(
    const Models::Track& current, const Models::Track& candidate,
    const std::vector<Models::Track>& history) const {

    QuantumState state;
    state.harmonicProbability = harmonicProbability(current, candidate);
    state.energyProbability = energyProbability(current, candidate);
    state.rhythmicProbability = rhythmicProbability(current, candidate);
    state.culturalProbability = culturalProbability(candidate, history);
    return state;
}

float QuantumSuggestionEngine::collapseState(const QuantumState& state) const {
    float base = state.harmonicProbability * 0.3f +
                 state.energyProbability * 0.25f +
                 state.rhythmicProbability * 0.25f +
                 state.culturalProbability * 0.2f;

    float alignment = 0.0f;
    int highFactors = 0;
    if (state.harmonicProbability > 0.7f) ++highFactors;
    if (state.energyProbability > 0.7f) ++highFactors;
    if (state.rhythmicProbability > 0.7f) ++highFactors;
    if (state.culturalProbability > 0.7f) ++highFactors;

    if (highFactors >= 3) alignment = 0.1f;
    if (highFactors >= 4) alignment = 0.2f;

    return std::min(1.0f, base + alignment);
}

float QuantumSuggestionEngine::harmonicProbability(const Models::Track& a, const Models::Track& b) const {
    std::string key1 = a.camelotKey.empty() ? a.key : a.camelotKey;
    std::string key2 = b.camelotKey.empty() ? b.key : b.camelotKey;
    if (key1.empty() || key2.empty()) return 0.5f;
    if (key1 == key2) return 1.0f;
    if (isKeyCompatible(key1, key2)) return 0.85f;
    return 0.1f;
}

float QuantumSuggestionEngine::energyProbability(const Models::Track& a, const Models::Track& b) const {
    float diff = std::abs(a.energy - b.energy);
    return std::max(0.0f, 1.0f - diff / 5.0f);
}

float QuantumSuggestionEngine::rhythmicProbability(const Models::Track& a, const Models::Track& b) const {
    if (a.bpm <= 0 || b.bpm <= 0) return 0.5f;
    double diff = std::abs(a.bpm - b.bpm);
    if (diff <= 2.0) return 1.0f;
    if (diff <= 6.0) return 0.8f;
    double halfDiff = std::abs(a.bpm - b.bpm * 2.0);
    if (halfDiff <= 4.0) return 0.7f;
    return std::max(0.0f, static_cast<float>(1.0 - diff / 20.0));
}

float QuantumSuggestionEngine::culturalProbability(const Models::Track& candidate, const std::vector<Models::Track>& history) const {
    if (history.empty()) return 0.5f;

    int genreMatch = 0;
    float avgEnergy = 0.0f;
    for (const auto& h : history) {
        if (h.genre == candidate.genre) ++genreMatch;
        avgEnergy += h.energy;
    }
    avgEnergy /= static_cast<float>(history.size());

    float genreScore = static_cast<float>(genreMatch) / static_cast<float>(history.size());
    float energyFit = 1.0f - std::abs(candidate.energy - avgEnergy) / 10.0f;
    return genreScore * 0.5f + energyFit * 0.5f;
}

bool QuantumSuggestionEngine::isKeyCompatible(const std::string& key1, const std::string& key2) const {
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
