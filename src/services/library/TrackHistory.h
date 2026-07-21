#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

#include "../../models/Track.h"

namespace BeatMate::Services::Library {

class TrackDatabase;

struct HistoryEntry {
    int64_t id = 0;
    int64_t trackId = 0;
    int64_t playedAt = 0;
    std::string context;
    Models::Track track; // populated on retrieval
};

class TrackHistory {
public:
    explicit TrackHistory(std::shared_ptr<TrackDatabase> database);
    ~TrackHistory() = default;

    bool recordPlay(int64_t trackId, const std::string& context = "");

    std::vector<HistoryEntry> getHistory(int limit = 100);
    std::vector<HistoryEntry> getHistoryForTrack(int64_t trackId, int limit = 50);
    std::vector<HistoryEntry> getHistoryBetween(int64_t startTime, int64_t endTime);

    std::vector<Models::Track> getMostPlayed(int limit = 50);
    std::vector<Models::Track> getRecentlyPlayed(int limit = 50);
    int getPlayCount(int64_t trackId);
    int getTotalPlays();

    bool clearHistory();
    bool clearHistoryBefore(int64_t timestamp);

private:
    std::shared_ptr<TrackDatabase> database_;
};

} // namespace BeatMate::Services::Library
