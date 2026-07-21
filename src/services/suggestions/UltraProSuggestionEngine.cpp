#include "UltraProSuggestionEngine.h"
#include "../library/TrackDatabase.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <iterator>

namespace BeatMate::Services::Suggestions {

UltraProSuggestionEngine::UltraProSuggestionEngine(std::shared_ptr<Library::TrackDatabase> database)
    : database_(std::move(database)) {}

std::vector<ProSuggestion> UltraProSuggestionEngine::suggest(const Models::Track& current, int count) {
    return suggestForMoment(current, current.energy, count);
}

std::vector<ProSuggestion> UltraProSuggestionEngine::suggestForMoment(
    const Models::Track& current, float targetEnergy, int count) {

    if (!database_) return {};
    auto allTracks = database_->getAllTracks();
    std::vector<ProSuggestion> results;

    std::vector<Models::Track> filtered;
    filtered.reserve(allTracks.size());
    const double curBpm = current.bpm;
    auto bpmAcceptable = [curBpm](double cand) -> bool {
        if (curBpm <= 0.0 || cand <= 0.0) return true; // missing data → keep
        const double diff1 = std::abs(curBpm - cand) / curBpm;
        const double diff2 = std::abs(curBpm - cand * 2.0) / curBpm;
        const double diffH = std::abs(curBpm - cand * 0.5) / curBpm;
        return diff1 < 0.20 || diff2 < 0.20 || diffH < 0.20;
    };
    std::copy_if(allTracks.begin(), allTracks.end(), std::back_inserter(filtered),
                 [&](const Models::Track& t) {
                     return t.id != current.id && bpmAcceptable(t.bpm);
                 });

    for (const auto& candidate : filtered) {
        if (candidate.id == current.id) continue;

        ProSuggestion sug;
        sug.track = candidate;
        sug.mixability = calculateMixability(current, candidate);
        sug.crowdImpact = calculateCrowdImpact(candidate, targetEnergy);
        sug.technicalFit = calculateTechnicalFit(current, candidate);
        sug.beatGridAlignment = calculateBeatGridAlignment(current, candidate);

        sug.score = sug.mixability * 0.3f + sug.crowdImpact * 0.25f +
                    sug.technicalFit * 0.25f + sug.beatGridAlignment * 0.2f;

        double bpmDiff = std::abs(current.bpm - candidate.bpm);
        std::string key1 = current.camelotKey.empty() ? current.key : current.camelotKey;
        std::string key2 = candidate.camelotKey.empty() ? candidate.key : candidate.camelotKey;
        bool keyCompat = isKeyCompatible(key1, key2) || key1 == key2;

        sug.mixTechnique = recommendMixTechnique(sug.mixability, static_cast<float>(bpmDiff), keyCompat);

        if (keyCompat && bpmDiff <= 4.0) sug.transitionAdvice = "Long harmonic blend (32 beats)";
        else if (bpmDiff <= 8.0) sug.transitionAdvice = "EQ blend with filter sweep (16 beats)";
        else sug.transitionAdvice = "Quick cut or echo out";

        if (sug.score > 0.2f) results.push_back(sug);
    }

    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    if (static_cast<int>(results.size()) > count) results.resize(static_cast<size_t>(count));
    spdlog::info("UltraProSuggestionEngine: {} pro suggestions for '{}'", results.size(), current.title);
    return results;
}

float UltraProSuggestionEngine::calculateMixability(const Models::Track& a, const Models::Track& b) const {
    float bpmScore = 0.0f;
    if (a.bpm > 0 && b.bpm > 0) {
        double diff = std::abs(a.bpm - b.bpm);
        bpmScore = std::max(0.0f, static_cast<float>(1.0 - diff / 10.0));
    }

    std::string key1 = a.camelotKey.empty() ? a.key : a.camelotKey;
    std::string key2 = b.camelotKey.empty() ? b.key : b.camelotKey;
    float keyScore = (key1 == key2) ? 1.0f : (isKeyCompatible(key1, key2) ? 0.85f : 0.1f);

    return bpmScore * 0.5f + keyScore * 0.5f;
}

float UltraProSuggestionEngine::calculateCrowdImpact(const Models::Track& candidate, float targetEnergy) const {
    float energyFit = 1.0f - std::abs(candidate.energy - targetEnergy) / 10.0f;
    float popularity = std::min(1.0f, static_cast<float>(candidate.playCount) / 30.0f);
    float rating = static_cast<float>(candidate.rating) / 5.0f;
    return energyFit * 0.5f + popularity * 0.3f + rating * 0.2f;
}

float UltraProSuggestionEngine::calculateTechnicalFit(const Models::Track& a, const Models::Track& b) const {
    float genreMatch = (!a.genre.empty() && a.genre == b.genre) ? 1.0f : 0.3f;
    float energySmooth = 1.0f - std::abs(a.energy - b.energy) / 10.0f;
    return genreMatch * 0.4f + energySmooth * 0.6f;
}

float UltraProSuggestionEngine::calculateBeatGridAlignment(const Models::Track& a, const Models::Track& b) const {
    if (a.bpm <= 0 || b.bpm <= 0) return 0.5f;

    double ratio = a.bpm / b.bpm;
    double rounded = std::round(ratio);
    if (std::abs(ratio - rounded) < 0.02 && rounded >= 1.0) return 1.0f;

    const double ratioAB = a.bpm / (b.bpm * 2.0);
    const double ratioBA = (a.bpm * 2.0) / b.bpm;
    const double rABr = std::round(ratioAB);
    const double rBAr = std::round(ratioBA);
    const bool halfHit =
        (std::abs(ratioAB - rABr) < 0.02 && rABr >= 1.0) ||
        (std::abs(ratioBA - rBAr) < 0.02 && rBAr >= 1.0);
    if (halfHit) return 0.8f;

    const double remainder = std::abs(ratio - rounded);
    return std::max(0.0f, static_cast<float>(1.0 - remainder * 10.0));
}

std::string UltraProSuggestionEngine::recommendMixTechnique(float mixability, float bpmDiff, bool keyCompatible) const {
    if (mixability > 0.8f && keyCompatible) return "Harmonic Long Blend";
    if (mixability > 0.6f) return "EQ Crossfade";
    if (bpmDiff <= 4.0f) return "Filter Sweep";
    if (bpmDiff <= 10.0f) return "Echo Out/In";
    return "Hard Cut";
}

bool UltraProSuggestionEngine::isKeyCompatible(const std::string& key1, const std::string& key2) const {
    if (key1.empty() || key2.empty() || key1.size() < 2 || key2.size() < 2) return false;
    try {
        int num1 = std::stoi(key1.substr(0, key1.size() - 1));
        int num2 = std::stoi(key2.substr(0, key2.size() - 1));
        char let1 = key1.back(); char let2 = key2.back();
        if (num1 == num2 && let1 == let2) return true;
        if (num1 == num2 && let1 != let2) return true;
        int diff = ((num2 - num1) + 12) % 12;
        return (diff == 1 || diff == 11) && let1 == let2;
    } catch (...) {}
    return false;
}

} // namespace BeatMate::Services::Suggestions
