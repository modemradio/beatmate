#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "../../models/StreamingTrack.h"

namespace BeatMate::Services::Streaming {

struct StreamingHistoryEntry {
    int64_t id = 0;
    Models::StreamingServiceType service;
    std::string serviceTrackId;
    std::string title;
    std::string artist;
    int64_t playedAt = 0;
};

class StreamingHistory {
public:
    StreamingHistory() = default;
    ~StreamingHistory() = default;

    bool recordPlay(Models::StreamingServiceType service, const std::string& trackId,
                    const std::string& title, const std::string& artist);
    std::vector<StreamingHistoryEntry> getHistory(int limit = 100);
    std::vector<StreamingHistoryEntry> getHistoryForService(Models::StreamingServiceType service, int limit = 50);
    bool clearHistory();

private:
    std::vector<StreamingHistoryEntry> history_; // In-memory for simplicity; DB-backed in production
};

} // namespace BeatMate::Services::Streaming
