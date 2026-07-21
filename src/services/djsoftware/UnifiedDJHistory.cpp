#include "UnifiedDJHistory.h"

#include "SeratoHistoryReader.h"
#include "RekordboxHistoryReader.h"
#include "TraktorHistoryReader.h"
#include "VirtualDJHistoryReader.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>

namespace BeatMate::Services::DJSoftware {

UnifiedDJHistory::UnifiedDJHistory()
{
    readers_.emplace_back(std::make_unique<SeratoHistoryReader>());
    readers_.emplace_back(std::make_unique<RekordboxHistoryReader>());
    readers_.emplace_back(std::make_unique<TraktorHistoryReader>());
    readers_.emplace_back(std::make_unique<VirtualDJHistoryReader>());

    for (auto& r : readers_) {
        if (!r) continue;
        statuses_[r->sourceName()] = Status::Disconnected;
        enabled_ [r->sourceName()] = false;
    }
    spdlog::info("[UnifiedDJHistory] initialized with {} readers", readers_.size());
}

UnifiedDJHistory::~UnifiedDJHistory()
{
    stopPolling();
}

std::string UnifiedDJHistory::trackKey(const PlayedTrack& t)
{
    return t.source + "|" + t.filePath + "|" + std::to_string(t.playedAtUnix)
         + "|" + t.title + "|" + t.artist;
}

std::vector<PlayedTrack> UnifiedDJHistory::getRecent(int maxTracks)
{
    std::vector<PlayedTrack> merged;

    for (auto& r : readers_) {
        if (!r) continue;
        try {
            auto batch = r->readRecentHistory(maxTracks);
            merged.insert(merged.end(),
                          std::make_move_iterator(batch.begin()),
                          std::make_move_iterator(batch.end()));
        } catch (const std::exception& e) {
            spdlog::warn("[UnifiedDJHistory] reader '{}' threw: {}",
                         r->sourceName(), e.what());
        } catch (...) {
            spdlog::warn("[UnifiedDJHistory] reader '{}' threw (unknown)",
                         r->sourceName());
        }
    }

    std::sort(merged.begin(), merged.end(),
        [](const PlayedTrack& a, const PlayedTrack& b) {
            return a.playedAtUnix > b.playedAtUnix;
        });

    if ((int) merged.size() > maxTracks) merged.resize(maxTracks);
    return merged;
}

void UnifiedDJHistory::startPolling(int intervalSec)
{
    if (polling_.load()) return;
    intervalSec_ = intervalSec > 0 ? intervalSec : 10;

    {
        std::lock_guard<std::mutex> lock(seenMutex_);
        seenKeys_.clear();
        for (auto& t : getRecent(500)) seenKeys_.insert(trackKey(t));
    }

    polling_.store(true);
    pollThread_ = std::thread([this]() {
        spdlog::info("[UnifiedDJHistory] polling started ({}s interval)", intervalSec_);
        while (polling_.load()) {
            try { pollOnce(); }
            catch (const std::exception& e) {
                spdlog::warn("[UnifiedDJHistory] poll failed: {}", e.what());
            } catch (...) {
                spdlog::warn("[UnifiedDJHistory] poll failed (unknown)");
            }
            for (int i = 0; i < intervalSec_ * 10 && polling_.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        spdlog::info("[UnifiedDJHistory] polling stopped");
    });
}

void UnifiedDJHistory::stopPolling()
{
    if (!polling_.load()) return;
    polling_.store(false);
    if (pollThread_.joinable()) pollThread_.join();
}

void UnifiedDJHistory::pollOnce()
{
    for (auto& r : readers_) {
        if (!r) continue;
        const std::string src = r->sourceName();

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = enabled_.find(src);
            if (it == enabled_.end() || !it->second) continue;
        }

        std::vector<PlayedTrack> recent;
        bool readerFailed = false;
        try {
            if (auto np = r->readNowPlaying()) recent.push_back(*np);
            auto hist = r->readRecentHistory(5);
            recent.insert(recent.end(),
                          std::make_move_iterator(hist.begin()),
                          std::make_move_iterator(hist.end()));
        } catch (...) {
            readerFailed = true;
        }

        if (readerFailed) {
            setStatus(src, Status::Error);
            continue;
        }

        if (recent.empty()) {
            setStatus(src, Status::Installed);
        } else {
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                nowPlaying_[src] = recent.front();
            }
            setStatus(src, Status::Connected);
        }

        for (auto& t : recent) {
            auto key = trackKey(t);
            bool isNew = false;
            {
                std::lock_guard<std::mutex> lock(seenMutex_);
                isNew = seenKeys_.insert(key).second;
            }
            if (isNew && onNewPlay) {
                try { onNewPlay(t); }
                catch (const std::exception& e) {
                    spdlog::warn("[UnifiedDJHistory] onNewPlay threw: {}", e.what());
                } catch (...) {
                    spdlog::warn("[UnifiedDJHistory] onNewPlay threw (unknown)");
                }
            }
        }
    }
}


bool UnifiedDJHistory::start(const std::string& sourceName)
{
    bool known = false;
    for (auto& r : readers_)
        if (r && sourceName == r->sourceName()) { known = true; break; }
    if (!known) {
        spdlog::warn("[UnifiedDJHistory] start() unknown source '{}'", sourceName);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        enabled_[sourceName] = true;
    }
    setStatus(sourceName, Status::Installed);
    if (!polling_.load()) startPolling(intervalSec_ > 0 ? intervalSec_ : 5);
    spdlog::info("[UnifiedDJHistory] source '{}' started", sourceName);
    return true;
}

void UnifiedDJHistory::stop(const std::string& sourceName)
{
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        enabled_[sourceName] = false;
        nowPlaying_.erase(sourceName);
    }
    setStatus(sourceName, Status::Disconnected);
    spdlog::info("[UnifiedDJHistory] source '{}' stopped", sourceName);
}

UnifiedDJHistory::Status UnifiedDJHistory::status(const std::string& sourceName) const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    auto it = statuses_.find(sourceName);
    if (it == statuses_.end()) return Status::Disconnected;
    return it->second;
}

std::optional<PlayedTrack> UnifiedDJHistory::nowPlaying(const std::string& sourceName) const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    auto it = nowPlaying_.find(sourceName);
    if (it == nowPlaying_.end()) return std::nullopt;
    return it->second;
}

void UnifiedDJHistory::setStatus(const std::string& src, Status s)
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        auto it = statuses_.find(src);
        if (it == statuses_.end() || it->second != s) {
            statuses_[src] = s;
            changed = true;
        }
    }
    if (changed) {
        const char* label = (s == Status::Connected    ? "Connected"    :
                             s == Status::Installed    ? "Installed"    :
                             s == Status::Error        ? "Error"        : "Disconnected");
        spdlog::info("[UnifiedDJHistory] status '{}' -> {}", src, label);
        if (onStatusChanged) {
            try { onStatusChanged(src, s); } catch (...) {}
        }
    }
}

} // namespace BeatMate::Services::DJSoftware
