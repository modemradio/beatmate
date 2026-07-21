#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include "SuggestionOrchestrator.h"
#include "../../models/Track.h"

namespace BeatMate::Services::Suggestions {

enum class SuggestionAction { AddToPlaylist, AddToSet, Preview, ShowInfo, LoadToDeck, Reject, Favorite };

struct ActionResult {
    bool success = false;
    SuggestionAction action;
    int64_t trackId = 0;
    std::string message;
};

using ActionCallback = std::function<void(const ActionResult&)>;

class SuggestionActionService {
public:
    SuggestionActionService() = default;

    ActionResult executeAction(SuggestionAction action, const RecommendationResult& suggestion);
    ActionResult addToPlaylist(const RecommendationResult& suggestion, int64_t playlistId);
    ActionResult addToSet(const RecommendationResult& suggestion, int64_t setId);
    ActionResult preview(const RecommendationResult& suggestion);
    ActionResult showInfo(const RecommendationResult& suggestion);
    ActionResult loadToDeck(const RecommendationResult& suggestion, int deckNumber);
    ActionResult reject(const RecommendationResult& suggestion);
    ActionResult favorite(const RecommendationResult& suggestion);

    void setActionCallback(ActionCallback callback) { callback_ = std::move(callback); }
    std::vector<int64_t> getRejectedIds() const { return rejectedIds_; }
    std::vector<int64_t> getFavoriteIds() const { return favoriteIds_; }
    void clearRejections() { rejectedIds_.clear(); }

private:
    void notifyCallback(const ActionResult& result);
    ActionCallback callback_;
    std::vector<int64_t> rejectedIds_;
    std::vector<int64_t> favoriteIds_;
};

} // namespace BeatMate::Services::Suggestions
