#include "UnifiedSuggestionService.h"
#include "../library/TrackDatabase.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <set>

namespace BeatMate::Services::Suggestions {

UnifiedSuggestionService::UnifiedSuggestionService(std::shared_ptr<Library::TrackDatabase> database)
    : database_(std::move(database)) {}

std::vector<UnifiedSuggestion> UnifiedSuggestionService::suggest(const UnifiedRequest& request) {
    switch (request.mode) {
        case SuggestionMode::Quick: return quickMode(request);
        case SuggestionMode::Standard: return standardMode(request);
        case SuggestionMode::Deep:
        case SuggestionMode::Expert: return deepMode(request);
    }
    return standardMode(request);
}

std::vector<UnifiedSuggestion> UnifiedSuggestionService::quickSuggest(const Models::Track& current, int count) {
    UnifiedRequest req;
    req.referenceTrack = current;
    req.mode = SuggestionMode::Quick;
    req.count = count;
    return suggest(req);
}

std::vector<UnifiedSuggestion> UnifiedSuggestionService::deepSuggest(const Models::Track& current, int count) {
    UnifiedRequest req;
    req.referenceTrack = current;
    req.mode = SuggestionMode::Deep;
    req.count = count;
    return suggest(req);
}

std::vector<UnifiedSuggestion> UnifiedSuggestionService::quickMode(const UnifiedRequest& request) {
    if (!database_) return {};
    auto allTracks = database_->getAllTracks();
    std::vector<UnifiedSuggestion> results;

    for (const auto& cand : allTracks) {
        if (cand.id == request.referenceTrack.id) continue;
        if (!passesFilter(cand, request)) continue;

        float score = computeScore(request.referenceTrack, cand);
        if (score > 0.4f) {
            UnifiedSuggestion sug;
            sug.track = cand;
            sug.score = score;
            sug.source = "quick";
            sug.reason = "BPM: " + std::to_string(static_cast<int>(cand.bpm)) + ", Key: " +
                         (cand.camelotKey.empty() ? cand.key : cand.camelotKey);
            results.push_back(sug);
        }
    }

    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    if (static_cast<int>(results.size()) > request.count) results.resize(static_cast<size_t>(request.count));
    spdlog::info("UnifiedSuggestionService: Quick - {} results", results.size());
    return results;
}

std::vector<UnifiedSuggestion> UnifiedSuggestionService::standardMode(const UnifiedRequest& request) {
    if (!database_) return {};
    auto allTracks = database_->getAllTracks();
    std::vector<UnifiedSuggestion> results;

    for (const auto& cand : allTracks) {
        if (cand.id == request.referenceTrack.id) continue;
        if (!passesFilter(cand, request)) continue;

        float score = computeScore(request.referenceTrack, cand);
        if (score > 0.3f) {
            UnifiedSuggestion sug;
            sug.track = cand;
            sug.score = score;
            sug.source = "standard";

            std::string key1 = request.referenceTrack.camelotKey.empty() ? request.referenceTrack.key : request.referenceTrack.camelotKey;
            std::string key2 = cand.camelotKey.empty() ? cand.key : cand.camelotKey;
            bool keyOk = key1 == key2 || isKeyCompatible(key1, key2);

            sug.scores["bpm"] = std::max(0.0f, static_cast<float>(1.0 - std::abs(request.referenceTrack.bpm - cand.bpm) / 10.0));
            sug.scores["key"] = keyOk ? 0.9f : 0.1f;
            sug.scores["energy"] = std::max(0.0f, 1.0f - std::abs(request.referenceTrack.energy - cand.energy) / 5.0f);

            double bpmDiff = std::abs(request.referenceTrack.bpm - cand.bpm);
            if (keyOk && bpmDiff <= 4.0) sug.transitionHint = "Long harmonic blend";
            else if (bpmDiff <= 8.0) sug.transitionHint = "EQ crossfade";
            else sug.transitionHint = "Quick cut";

            sug.reason = "Score: " + std::to_string(static_cast<int>(score * 100)) + "%";
            results.push_back(sug);
        }
    }

    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    if (static_cast<int>(results.size()) > request.count) results.resize(static_cast<size_t>(request.count));
    spdlog::info("UnifiedSuggestionService: Standard - {} results", results.size());
    return results;
}

std::vector<UnifiedSuggestion> UnifiedSuggestionService::deepMode(const UnifiedRequest& request) {
    auto results = standardMode(request);
    for (auto& r : results) {
        r.source = "deep";
        float genreScore = (!request.referenceTrack.genre.empty() && request.referenceTrack.genre == r.track.genre) ? 1.0f : 0.2f;
        float moodScore = (!request.referenceTrack.mood.empty() && request.referenceTrack.mood == r.track.mood) ? 1.0f : 0.3f;
        r.scores["genre"] = genreScore;
        r.scores["mood"] = moodScore;
        r.score = r.score * 0.7f + genreScore * 0.15f + moodScore * 0.15f;
    }

    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    spdlog::info("UnifiedSuggestionService: Deep - {} results", results.size());
    return results;
}

float UnifiedSuggestionService::computeScore(const Models::Track& ref, const Models::Track& cand) const {
    float bpmScore = 0.0f;
    if (ref.bpm > 0 && cand.bpm > 0) {
        bpmScore = std::max(0.0f, static_cast<float>(1.0 - std::abs(ref.bpm - cand.bpm) / 10.0));
    }

    std::string key1 = ref.camelotKey.empty() ? ref.key : ref.camelotKey;
    std::string key2 = cand.camelotKey.empty() ? cand.key : cand.camelotKey;
    float keyScore = (key1 == key2) ? 1.0f : (isKeyCompatible(key1, key2) ? 0.85f : 0.1f);

    float energyScore = std::max(0.0f, 1.0f - std::abs(ref.energy - cand.energy) / 5.0f);
    float genreScore = (!ref.genre.empty() && ref.genre == cand.genre) ? 1.0f : 0.3f;

    return bpmScore * 0.3f + keyScore * 0.3f + energyScore * 0.2f + genreScore * 0.2f;
}

bool UnifiedSuggestionService::passesFilter(const Models::Track& candidate, const UnifiedRequest& request) const {
    std::set<int64_t> excludeSet(request.excludeIds.begin(), request.excludeIds.end());
    if (excludeSet.count(candidate.id)) return false;

    if (request.bpmTolerance > 0 && request.referenceTrack.bpm > 0 && candidate.bpm > 0) {
        double bpmPercent = std::abs(request.referenceTrack.bpm - candidate.bpm) / request.referenceTrack.bpm * 100.0;
        if (bpmPercent > request.bpmTolerance) return false;
    }

    if (request.onlyCompatibleKeys) {
        std::string key1 = request.referenceTrack.camelotKey.empty() ? request.referenceTrack.key : request.referenceTrack.camelotKey;
        std::string key2 = candidate.camelotKey.empty() ? candidate.key : candidate.camelotKey;
        if (!key1.empty() && !key2.empty() && key1 != key2 && !isKeyCompatible(key1, key2)) return false;
    }

    if (!request.preferredGenre.empty() && candidate.genre != request.preferredGenre) return false;

    return true;
}

bool UnifiedSuggestionService::isKeyCompatible(const std::string& key1, const std::string& key2) const {
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
