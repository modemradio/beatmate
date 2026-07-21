#include "HarmonicSuggestionEngine.h"
#include "../library/TrackDatabase.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace BeatMate::Services::Suggestions {

const std::map<std::string, std::string> HarmonicSuggestionEngine::openKeyMap_ = {
    {"Am", "1A"}, {"Em", "2A"}, {"Bm", "3A"}, {"F#m", "4A"}, {"C#m", "5A"}, {"G#m", "6A"},
    {"D#m", "7A"}, {"A#m", "8A"}, {"Fm", "9A"}, {"Cm", "10A"}, {"Gm", "11A"}, {"Dm", "12A"},
    {"C", "1B"}, {"G", "2B"}, {"D", "3B"}, {"A", "4B"}, {"E", "5B"}, {"B", "6B"},
    {"F#", "7B"}, {"C#", "8B"}, {"Ab", "9B"}, {"Eb", "10B"}, {"Bb", "11B"}, {"F", "12B"}
};

HarmonicSuggestionEngine::HarmonicSuggestionEngine(std::shared_ptr<Library::TrackDatabase> database)
    : database_(std::move(database)) {}

std::vector<RecommendationResult> HarmonicSuggestionEngine::suggest(const Models::Track& current, int count) {
    if (!database_) {
        spdlog::debug("HarmonicSuggestionEngine: No database, skipping suggest");
        return {};
    }

    std::string currentKey = !current.camelotKey.empty() ? current.camelotKey : current.key;
    if (openKeyMap_.count(currentKey)) currentKey = openKeyMap_.at(currentKey);

    const bool currentHasKey = !currentKey.empty();
    auto compatibleKeys = currentHasKey ? getCompatibleKeys(currentKey)
                                        : std::vector<std::string>{};

    auto allTracks = database_->getAllTracks();
    std::vector<RecommendationResult> results;
    results.reserve(allTracks.size());

    const double curBpm = current.bpm;

    for (const auto& track : allTracks) {
        if (track.id == current.id) continue;

        std::string trackKey = !track.camelotKey.empty() ? track.camelotKey : track.key;
        if (openKeyMap_.count(trackKey)) trackKey = openKeyMap_.at(trackKey);

        RecommendationResult result;
        result.track = track;
        float score = 0.0f;
        std::string reason;

        if (currentHasKey && !trackKey.empty()) {
            auto it = std::find(compatibleKeys.begin(), compatibleKeys.end(), trackKey);
            if (it != compatibleKeys.end()) {
                if (trackKey == currentKey) {
                    score = 1.00f;
                    reason = "Same key (" + trackKey + ")";
                } else if (trackKey.back() != currentKey.back()
                        && trackKey.substr(0, trackKey.size()-1) == currentKey.substr(0, currentKey.size()-1)) {
                    score = 0.90f;
                    reason = "Parallel key (" + currentKey + " > " + trackKey + ")";
                } else {
                    score = 0.80f;
                    reason = "Adjacent key (" + currentKey + " > " + trackKey + ")";
                }
            } else {
                score  = 0.25f;
                reason = "Key " + trackKey + " (incompatible)";
            }
        } else if (!currentHasKey && !trackKey.empty()) {
            score  = 0.40f;
            reason = "Key " + trackKey + " (reference key unknown)";
        } else {
            score  = 0.35f;
            reason = "BPM match (no key)";
        }

        if (curBpm > 0.0 && track.bpm > 0.0) {
            double r1 = std::abs(track.bpm / curBpm - 1.0);
            double r2 = std::abs((track.bpm * 2.0) / curBpm - 1.0);
            double r3 = std::abs(track.bpm / (curBpm * 2.0) - 1.0);
            double best = std::min({r1, r2, r3});
            if (best <= 0.06) {
                float bonus = static_cast<float>(0.20 * (1.0 - best / 0.06));
                score += bonus;
            } else if (best <= 0.15) {
                score += 0.05f;
            }
        }

        if (!current.genre.empty() && !track.genre.empty()) {
            auto lower = [](std::string s) {
                for (auto& c : s) c = (char) std::tolower((unsigned char) c);
                return s;
            };
            if (lower(current.genre) == lower(track.genre)) {
                score += 0.10f;
            }
        }

        if (score > 1.0f) score = 1.0f;
        if (score < 0.0f) score = 0.0f;

        result.score = score;
        result.reason = reason;
        result.componentScores["harmonic"] = score;
        results.push_back(std::move(result));
    }

    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) { return a.score > b.score; });

    if (static_cast<int>(results.size()) > count) results.resize(static_cast<size_t>(count));
    spdlog::info("HarmonicSuggestionEngine: {} results for '{}' (soft-scoring, count={})",
                 results.size(), current.title, count);
    return results;
}

std::vector<std::string> HarmonicSuggestionEngine::getCompatibleKeys(const std::string& camelotKey) {
    std::vector<std::string> compatible;
    if (camelotKey.size() < 2) return compatible;

    try {
        int num = std::stoi(camelotKey.substr(0, camelotKey.size() - 1));
        char letter = camelotKey.back();

        compatible.push_back(camelotKey);
        int plus1 = (num % 12) + 1;
        compatible.push_back(std::to_string(plus1) + letter);
        int minus1 = ((num - 2 + 12) % 12) + 1;
        compatible.push_back(std::to_string(minus1) + letter);
        char parallel = (letter == 'A') ? 'B' : 'A';
        compatible.push_back(std::to_string(num) + parallel);
    } catch (...) {}

    return compatible;
}

std::string HarmonicSuggestionEngine::openKeyToCamelot(const std::string& openKey) {
    auto it = openKeyMap_.find(openKey);
    return (it != openKeyMap_.end()) ? it->second : "";
}

std::string HarmonicSuggestionEngine::camelotToOpenKey(const std::string& camelotKey) {
    for (const auto& [ok, ck] : openKeyMap_) {
        if (ck == camelotKey) return ok;
    }
    return "";
}

}
