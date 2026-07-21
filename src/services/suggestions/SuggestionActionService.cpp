#include "SuggestionActionService.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace BeatMate::Services::Suggestions {

ActionResult SuggestionActionService::executeAction(SuggestionAction action, const RecommendationResult& suggestion) {
    switch (action) {
        case SuggestionAction::AddToPlaylist: return addToPlaylist(suggestion, 0);
        case SuggestionAction::AddToSet: return addToSet(suggestion, 0);
        case SuggestionAction::Preview: return preview(suggestion);
        case SuggestionAction::ShowInfo: return showInfo(suggestion);
        case SuggestionAction::LoadToDeck: return loadToDeck(suggestion, 1);
        case SuggestionAction::Reject: return reject(suggestion);
        case SuggestionAction::Favorite: return favorite(suggestion);
    }
    return {false, action, suggestion.track.id, "Unknown action"};
}

ActionResult SuggestionActionService::addToPlaylist(const RecommendationResult& suggestion, int64_t playlistId) {
    ActionResult result;
    result.action = SuggestionAction::AddToPlaylist;
    result.trackId = suggestion.track.id;
    result.success = true;
    result.message = "Added '" + suggestion.track.title + "' to playlist " + std::to_string(playlistId);
    spdlog::info("SuggestionActionService: {}", result.message);
    notifyCallback(result);
    return result;
}

ActionResult SuggestionActionService::addToSet(const RecommendationResult& suggestion, int64_t setId) {
    ActionResult result;
    result.action = SuggestionAction::AddToSet;
    result.trackId = suggestion.track.id;
    result.success = true;
    result.message = "Added '" + suggestion.track.title + "' to set " + std::to_string(setId);
    spdlog::info("SuggestionActionService: {}", result.message);
    notifyCallback(result);
    return result;
}

ActionResult SuggestionActionService::preview(const RecommendationResult& suggestion) {
    ActionResult result;
    result.action = SuggestionAction::Preview;
    result.trackId = suggestion.track.id;
    result.success = true;
    result.message = "Previewing '" + suggestion.track.title + "' by " + suggestion.track.artist;
    spdlog::info("SuggestionActionService: {}", result.message);
    notifyCallback(result);
    return result;
}

ActionResult SuggestionActionService::showInfo(const RecommendationResult& suggestion) {
    ActionResult result;
    result.action = SuggestionAction::ShowInfo;
    result.trackId = suggestion.track.id;
    result.success = true;

    const auto& t = suggestion.track;
    result.message = "Track Info:\n"
        "Title: " + t.title + "\n"
        "Artist: " + t.artist + "\n"
        "BPM: " + std::to_string(static_cast<int>(t.bpm)) + "\n"
        "Key: " + (t.camelotKey.empty() ? t.key : t.camelotKey) + "\n"
        "Energy: " + std::to_string(static_cast<int>(t.energy)) + "/10\n"
        "Genre: " + t.genre + "\n"
        "Score: " + std::to_string(static_cast<int>(suggestion.score * 100)) + "%\n"
        "Reason: " + suggestion.reason;

    spdlog::info("SuggestionActionService: ShowInfo for '{}'", t.title);
    notifyCallback(result);
    return result;
}

ActionResult SuggestionActionService::loadToDeck(const RecommendationResult& suggestion, int deckNumber) {
    ActionResult result;
    result.action = SuggestionAction::LoadToDeck;
    result.trackId = suggestion.track.id;
    result.success = true;
    result.message = "Loading '" + suggestion.track.title + "' to Deck " + std::to_string(deckNumber);
    spdlog::info("SuggestionActionService: {}", result.message);
    notifyCallback(result);
    return result;
}

ActionResult SuggestionActionService::reject(const RecommendationResult& suggestion) {
    ActionResult result;
    result.action = SuggestionAction::Reject;
    result.trackId = suggestion.track.id;
    result.success = true;

    auto it = std::find(rejectedIds_.begin(), rejectedIds_.end(), suggestion.track.id);
    if (it == rejectedIds_.end()) {
        rejectedIds_.push_back(suggestion.track.id);
    }

    result.message = "Rejected '" + suggestion.track.title + "' - won't suggest again this session";
    spdlog::info("SuggestionActionService: {}", result.message);
    notifyCallback(result);
    return result;
}

ActionResult SuggestionActionService::favorite(const RecommendationResult& suggestion) {
    ActionResult result;
    result.action = SuggestionAction::Favorite;
    result.trackId = suggestion.track.id;
    result.success = true;

    auto it = std::find(favoriteIds_.begin(), favoriteIds_.end(), suggestion.track.id);
    if (it == favoriteIds_.end()) {
        favoriteIds_.push_back(suggestion.track.id);
    }

    result.message = "Favorited '" + suggestion.track.title + "'";
    spdlog::info("SuggestionActionService: {}", result.message);
    notifyCallback(result);
    return result;
}

void SuggestionActionService::notifyCallback(const ActionResult& result) {
    if (callback_) callback_(result);
}

} // namespace BeatMate::Services::Suggestions
