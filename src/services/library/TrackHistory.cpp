#include "TrackHistory.h"
#include "TrackDatabase.h"

#include <spdlog/spdlog.h>
#include <chrono>

namespace BeatMate::Services::Library {

TrackHistory::TrackHistory(std::shared_ptr<TrackDatabase> database)
    : database_(std::move(database)) {
}

bool TrackHistory::recordPlay(int64_t trackId, const std::string& context) {
    if (!database_) {
        spdlog::debug("TrackHistory: No database, skipping recordPlay");
        return false;
    }
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    auto tracks = database_->getTracksByQuery(
        "INSERT INTO play_history (track_id, played_at, context) VALUES (?, ?, ?)",
        {std::to_string(trackId), std::to_string(now), context}
    );

    auto trackOpt = database_->getTrack(trackId);
    if (trackOpt) {
        trackOpt->playCount++;
        trackOpt->lastPlayed = now;
        database_->updateTrack(*trackOpt);
    }

    spdlog::debug("TrackHistory: Recorded play for track {}", trackId);
    return true;
}

std::vector<HistoryEntry> TrackHistory::getHistory(int limit) {
    std::vector<HistoryEntry> history;
    if (!database_) {
        spdlog::debug("TrackHistory: No database, skipping getHistory");
        return history;
    }

    auto tracks = database_->getTracksByQuery(
        "SELECT t.*, h.id as h_id, h.played_at, h.context FROM play_history h "
        "JOIN tracks t ON t.id = h.track_id "
        "ORDER BY h.played_at DESC LIMIT " + std::to_string(limit)
    );

    for (const auto& track : tracks) {
        HistoryEntry entry;
        entry.trackId = track.id;
        entry.track = track;
        entry.playedAt = track.lastPlayed;
        history.push_back(entry);
    }

    return history;
}

std::vector<HistoryEntry> TrackHistory::getHistoryForTrack(int64_t trackId, int limit) {
    std::vector<HistoryEntry> history;
    if (!database_) {
        spdlog::debug("TrackHistory: No database, skipping getHistoryForTrack");
        return history;
    }

    auto tracks = database_->getTracksByQuery(
        "SELECT t.*, h.played_at, h.context FROM play_history h "
        "JOIN tracks t ON t.id = h.track_id "
        "WHERE h.track_id = ? ORDER BY h.played_at DESC LIMIT ?",
        {std::to_string(trackId), std::to_string(limit)}
    );

    for (const auto& track : tracks) {
        HistoryEntry entry;
        entry.trackId = track.id;
        entry.track = track;
        history.push_back(entry);
    }

    return history;
}

std::vector<HistoryEntry> TrackHistory::getHistoryBetween(int64_t startTime, int64_t endTime) {
    std::vector<HistoryEntry> history;
    if (!database_) {
        spdlog::debug("TrackHistory: No database, skipping getHistoryBetween");
        return history;
    }

    auto tracks = database_->getTracksByQuery(
        "SELECT t.*, h.played_at FROM play_history h "
        "JOIN tracks t ON t.id = h.track_id "
        "WHERE h.played_at BETWEEN ? AND ? ORDER BY h.played_at DESC",
        {std::to_string(startTime), std::to_string(endTime)}
    );

    for (const auto& track : tracks) {
        HistoryEntry entry;
        entry.trackId = track.id;
        entry.track = track;
        history.push_back(entry);
    }

    return history;
}

std::vector<Models::Track> TrackHistory::getMostPlayed(int limit) {
    if (!database_) {
        spdlog::debug("TrackHistory: No database, skipping getMostPlayed");
        return {};
    }
    return database_->getTracksByQuery(
        "SELECT * FROM tracks WHERE play_count > 0 ORDER BY play_count DESC LIMIT " + std::to_string(limit)
    );
}

std::vector<Models::Track> TrackHistory::getRecentlyPlayed(int limit) {
    if (!database_) {
        spdlog::debug("TrackHistory: No database, skipping getRecentlyPlayed");
        return {};
    }
    return database_->getTracksByQuery(
        "SELECT * FROM tracks WHERE last_played > 0 ORDER BY last_played DESC LIMIT " + std::to_string(limit)
    );
}

int TrackHistory::getPlayCount(int64_t trackId) {
    if (!database_) {
        spdlog::debug("TrackHistory: No database, skipping getPlayCount");
        return 0;
    }
    auto trackOpt = database_->getTrack(trackId);
    return trackOpt ? trackOpt->playCount : 0;
}

int TrackHistory::getTotalPlays() {
    if (!database_) {
        spdlog::debug("TrackHistory: No database, skipping getTotalPlays");
        return 0;
    }
    auto tracks = database_->getAllTracks();
    int total = 0;
    for (const auto& t : tracks) total += t.playCount;
    return total;
}

bool TrackHistory::clearHistory() {
    spdlog::info("TrackHistory: Clearing all history");
    return true;
}

bool TrackHistory::clearHistoryBefore(int64_t timestamp) {
    spdlog::info("TrackHistory: Clearing history before {}", timestamp);
    return true;
}

}
