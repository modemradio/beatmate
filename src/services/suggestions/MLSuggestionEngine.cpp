#include "MLSuggestionEngine.h"
#include "../library/TrackDatabase.h"
#include "../ai/ONNXInference.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace BeatMate::Services::Suggestions {

MLSuggestionEngine::MLSuggestionEngine(std::shared_ptr<Library::TrackDatabase> database)
    : database_(std::move(database)), inference_(std::make_shared<AI::ONNXInference>()) {}

std::vector<RecommendationResult> MLSuggestionEngine::suggest(const Models::Track& current, int count) {
    if (!database_) {
        spdlog::debug("MLSuggestionEngine: No database, skipping suggest");
        return {};
    }
    auto currentFeatures = embed(extractFeatures(current));
    auto allTracks = database_->getAllTracks();
    std::vector<RecommendationResult> results;

    for (const auto& candidate : allTracks) {
        if (candidate.id == current.id) continue;
        auto candidateFeatures = embed(extractFeatures(candidate));
        float similarity = cosineSimilarity(currentFeatures, candidateFeatures);

        if (similarity > 0.5f) {
            RecommendationResult result;
            result.track = candidate;
            result.score = similarity;
            result.reason = "ML similarity: " + std::to_string(static_cast<int>(similarity * 100)) + "%";
            result.componentScores["ml_similarity"] = similarity;
            results.push_back(result);
        }
    }

    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    if (static_cast<int>(results.size()) > count) results.resize(static_cast<size_t>(count));
    return results;
}

bool MLSuggestionEngine::loadModel(const std::string& modelPath) {
    modelPath_ = modelPath;
    modelLoaded_ = inference_ && inference_->loadModel(modelPath);
    if (modelLoaded_)
        spdlog::info("MLSuggestionEngine: Model loaded from {}", modelPath);
    else
        spdlog::warn("MLSuggestionEngine: Failed to load model {}, using heuristic features", modelPath);
    return modelLoaded_;
}

std::vector<float> MLSuggestionEngine::embed(const std::vector<float>& features) const {
    if (modelLoaded_ && inference_ && inference_->isLoaded()) {
        auto out = inference_->run(features);
        if (!out.empty()) return out;
    }
    return features;
}

std::vector<float> MLSuggestionEngine::extractFeatures(const Models::Track& track) const {
    return {
        static_cast<float>(track.bpm / 200.0),
        track.energy / 10.0f,
        track.danceability,
        static_cast<float>(track.duration / 600.0),
        static_cast<float>(track.year > 0 ? (track.year - 1950.0) / 80.0 : 0.5),
        static_cast<float>(track.rating / 5.0),
        static_cast<float>(track.bitRate / 320.0),
    };
}

float MLSuggestionEngine::cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) const {
    if (a.size() != b.size() || a.empty()) return 0.0f;

    float dot = 0.0f, normA = 0.0f, normB = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        normA += a[i] * a[i];
        normB += b[i] * b[i];
    }

    float denom = std::sqrt(normA) * std::sqrt(normB);
    return denom > 0.0f ? dot / denom : 0.0f;
}

} // namespace BeatMate::Services::Suggestions
