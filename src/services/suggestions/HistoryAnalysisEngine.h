#pragma once
#include <vector>
#include <memory>
#include "SuggestionOrchestrator.h"

namespace BeatMate::Services::Library { class TrackDatabase; class TrackHistory; }

namespace BeatMate::Services::Suggestions {

class HistoryAnalysisEngine {
public:
    HistoryAnalysisEngine(std::shared_ptr<Library::TrackDatabase> db, std::shared_ptr<Library::TrackHistory> history);
    ~HistoryAnalysisEngine() = default;
    std::vector<RecommendationResult> suggest(const Models::Track& current, int count = 10);
private:
    std::shared_ptr<Library::TrackDatabase> database_;
    std::shared_ptr<Library::TrackHistory> history_;
};

}
