#include "StreamingHistory.h"

#include <spdlog/spdlog.h>
#include <chrono>
#include <algorithm>

namespace BeatMate::Services::Streaming {

bool StreamingHistory::recordPlay(Models::StreamingServiceType service, const std::string& trackId,
                                   const std::string& title, const std::string& artist) {
    StreamingHistoryEntry entry;
    entry.id = static_cast<int64_t>(history_.size()) + 1;
    entry.service = service;
    entry.serviceTrackId = trackId;
    entry.title = title;
    entry.artist = artist;
    entry.playedAt = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    history_.push_back(entry);
    spdlog::debug("StreamingHistory: Recorded play: '{}' by '{}'", title, artist);
    return true;
}

std::vector<StreamingHistoryEntry> StreamingHistory::getHistory(int limit) {
    std::vector<StreamingHistoryEntry> result;
    int count = std::min(limit, static_cast<int>(history_.size()));

    for (int i = static_cast<int>(history_.size()) - 1; i >= 0 && count > 0; --i, --count) {
        result.push_back(history_[static_cast<size_t>(i)]);
    }

    return result;
}

std::vector<StreamingHistoryEntry> StreamingHistory::getHistoryForService(
    Models::StreamingServiceType service, int limit) {
    std::vector<StreamingHistoryEntry> result;

    for (auto it = history_.rbegin(); it != history_.rend() && static_cast<int>(result.size()) < limit; ++it) {
        if (it->service == service) {
            result.push_back(*it);
        }
    }

    return result;
}

bool StreamingHistory::clearHistory() {
    history_.clear();
    spdlog::info("StreamingHistory: History cleared");
    return true;
}

} // namespace BeatMate::Services::Streaming
