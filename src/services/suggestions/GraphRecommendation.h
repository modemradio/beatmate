#pragma once
#include <cstdint>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include "SuggestionOrchestrator.h"

namespace BeatMate::Services::Library { class TrackDatabase; }

namespace BeatMate::Services::Suggestions {

struct GraphEdge { int64_t targetId; float weight; };

class GraphRecommendation {
public:
    explicit GraphRecommendation(std::shared_ptr<Library::TrackDatabase> database);
    ~GraphRecommendation() = default;

    void buildGraph(float minSimilarity = 0.5f);
    std::vector<RecommendationResult> recommend(int64_t trackId, int count = 10);
    bool isGraphBuilt() const { return built_; }
    size_t nodeCount() const { return graph_.size(); }

private:
    std::map<int64_t, std::vector<GraphEdge>> graph_;
    std::shared_ptr<Library::TrackDatabase> database_;
    bool built_ = false;
};

} // namespace BeatMate::Services::Suggestions
