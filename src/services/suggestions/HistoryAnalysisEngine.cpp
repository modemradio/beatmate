#include "HistoryAnalysisEngine.h"
#include "../library/TrackDatabase.h"
#include "../library/TrackHistory.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace BeatMate::Services::Suggestions {

HistoryAnalysisEngine::HistoryAnalysisEngine(std::shared_ptr<Library::TrackDatabase> db,
                                               std::shared_ptr<Library::TrackHistory> history)
    : database_(std::move(db)), history_(std::move(history)) {}

std::vector<RecommendationResult> HistoryAnalysisEngine::suggest(const Models::Track& current, int count) {
    auto historyEntries = history_->getHistory(500);
    std::map<int64_t, int> followCount;

    for (size_t i = 0; i + 1 < historyEntries.size(); ++i) {
        if (historyEntries[i].trackId == current.id) {
            followCount[historyEntries[i + 1].trackId]++;
        }
    }

    std::vector<RecommendationResult> results;
    for (const auto& [trackId, cnt] : followCount) {
        auto trackOpt = database_->getTrack(trackId);
        if (!trackOpt || trackOpt->id == current.id) continue;

        RecommendationResult result;
        result.track = *trackOpt;
        result.score = std::min(1.0f, static_cast<float>(cnt) / 5.0f);
        result.reason = "Played " + std::to_string(cnt) + " times after this track";
        result.componentScores["history"] = result.score;
        results.push_back(result);
    }

    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    if (static_cast<int>(results.size()) > count) results.resize(static_cast<size_t>(count));
    spdlog::debug("HistoryAnalysisEngine: {} suggestions based on play history", results.size());
    return results;
}

} // namespace BeatMate::Services::Suggestions
