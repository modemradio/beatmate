#include "StreamingSuggestionService.h"
#include "../library/TrackDatabase.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace BeatMate::Services::Suggestions {

StreamingSuggestionService::StreamingSuggestionService(std::shared_ptr<Library::TrackDatabase> database)
    : database_(std::move(database)) {}

std::vector<StreamingSuggestion> StreamingSuggestionService::suggest(const Models::Track& current, int count) {
    return mergeLocalAndStreaming(current, count);
}

std::vector<StreamingSuggestion> StreamingSuggestionService::suggestFromStreaming(
    const Models::Track& current, const StreamingSearchConfig& config, int count) {

    auto localResults = getLocalSuggestions(current, count);

    std::vector<StreamingSuggestion> results;
    for (auto& sug : localResults) {
        if (config.preferLocal || sug.availableOffline) {
            results.push_back(sug);
        }
    }

    if (static_cast<int>(results.size()) > count) results.resize(static_cast<size_t>(count));
    spdlog::info("StreamingSuggestionService: {} streaming suggestions for '{}'", results.size(), current.title);
    return results;
}

std::vector<StreamingSuggestion> StreamingSuggestionService::mergeLocalAndStreaming(
    const Models::Track& current, int count) {

    auto localResults = getLocalSuggestions(current, count);

    for (auto& r : localResults) {
        if (r.streamingService.empty()) {
            r.streamingService = "local";
            r.availableOffline = true;
            r.score *= 1.05f; // slight bonus for local tracks
        }
    }

    std::sort(localResults.begin(), localResults.end(),
              [](const auto& a, const auto& b) { return a.score > b.score; });
    if (static_cast<int>(localResults.size()) > count) localResults.resize(static_cast<size_t>(count));
    return localResults;
}

std::vector<StreamingSuggestion> StreamingSuggestionService::getLocalSuggestions(
    const Models::Track& current, int count) {

    if (!database_) return {};
    auto allTracks = database_->getAllTracks();
    std::vector<StreamingSuggestion> results;

    for (const auto& candidate : allTracks) {
        if (candidate.id == current.id) continue;

        float score = computeScore(current, candidate);
        if (score > 0.3f) {
            StreamingSuggestion sug;
            sug.track = candidate;
            sug.score = score;
            sug.streamingService = (candidate.source == Models::TrackSource::Streaming) ? "streaming" : "local";
            sug.availableOffline = (candidate.source != Models::TrackSource::Streaming);
            sug.popularity = std::min(1.0f, static_cast<float>(candidate.playCount) / 50.0f);
            sug.reason = "Score: " + std::to_string(static_cast<int>(score * 100)) + "%";
            results.push_back(sug);
        }
    }

    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    if (static_cast<int>(results.size()) > count * 2) results.resize(static_cast<size_t>(count * 2));
    return results;
}

float StreamingSuggestionService::computeScore(const Models::Track& ref, const Models::Track& cand) const {
    float bpmScore = 0.0f;
    if (ref.bpm > 0 && cand.bpm > 0) {
        bpmScore = std::max(0.0f, static_cast<float>(1.0 - std::abs(ref.bpm - cand.bpm) / 10.0));
    }

    std::string key1 = ref.camelotKey.empty() ? ref.key : ref.camelotKey;
    std::string key2 = cand.camelotKey.empty() ? cand.key : cand.camelotKey;
    float keyScore = (key1 == key2) ? 1.0f : (isKeyCompatible(key1, key2) ? 0.85f : 0.1f);

    float energyScore = std::max(0.0f, 1.0f - std::abs(ref.energy - cand.energy) / 5.0f);

    return bpmScore * 0.35f + keyScore * 0.35f + energyScore * 0.3f;
}

bool StreamingSuggestionService::isKeyCompatible(const std::string& key1, const std::string& key2) const {
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
