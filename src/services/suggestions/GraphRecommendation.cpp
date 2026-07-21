#include "GraphRecommendation.h"
#include "TrackCompatibility.h"
#include "../library/TrackDatabase.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <queue>

namespace BeatMate::Services::Suggestions {

GraphRecommendation::GraphRecommendation(std::shared_ptr<Library::TrackDatabase> database)
    : database_(std::move(database)) {}

void GraphRecommendation::buildGraph(float minSimilarity) {
    auto tracks = database_->getAllTracks();
    graph_.clear();

    for (size_t i = 0; i < tracks.size(); ++i) {
        for (size_t j = i + 1; j < tracks.size(); ++j) {
            auto score = TrackCompatibility::calculateScore(tracks[i], tracks[j]);
            if (score.overall >= minSimilarity) {
                graph_[tracks[i].id].push_back({tracks[j].id, score.overall});
                graph_[tracks[j].id].push_back({tracks[i].id, score.overall});
            }
        }
    }

    built_ = true;
    spdlog::info("GraphRecommendation: Built graph with {} nodes", graph_.size());
}

std::vector<RecommendationResult> GraphRecommendation::recommend(int64_t trackId, int count) {
    if (!built_ || graph_.find(trackId) == graph_.end()) return {};

    // BFS with weighted edges
    std::set<int64_t> visited;
    std::priority_queue<std::pair<float, int64_t>> pq;
    pq.push({0.0f, trackId});
    visited.insert(trackId);

    std::vector<RecommendationResult> results;

    while (!pq.empty() && static_cast<int>(results.size()) < count) {
        auto [score, currentId] = pq.top();
        pq.pop();

        if (currentId != trackId) {
            auto trackOpt = database_->getTrack(currentId);
            if (trackOpt) {
                RecommendationResult result;
                result.track = *trackOpt;
                result.score = -score; // Priority queue is max-heap, we used negative scores
                result.reason = "Graph traversal similarity";
                results.push_back(result);
            }
        }

        if (graph_.find(currentId) != graph_.end()) {
            for (const auto& edge : graph_[currentId]) {
                if (visited.find(edge.targetId) == visited.end()) {
                    visited.insert(edge.targetId);
                    pq.push({score - edge.weight, edge.targetId}); // Negative for max-heap
                }
            }
        }
    }

    return results;
}

} // namespace BeatMate::Services::Suggestions
