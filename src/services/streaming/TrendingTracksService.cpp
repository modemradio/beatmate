#include "TrendingTracksService.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <numeric>

using json = nlohmann::json;

namespace BeatMate::Services::Streaming {

static std::string currentDateString() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &tt);
#else
    localtime_r(&tt, &tm_buf);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
    return buf;
}

static int64_t nowUnix() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

TrendingTracksService::TrendingTracksService() = default;


void TrendingTracksService::recordPlay(int64_t trackId, const std::string& title,
                                         const std::string& artist, const std::string& genre,
                                         double bpm, const std::string& key, int64_t userId) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto now = nowUnix();
    std::string today = currentDateString();

    auto& entry = playData_[trackId];
    if (entry.trackId == 0) {
        entry.trackId = trackId;
        entry.firstPlayedAt = now;
    }
    entry.title = title;
    entry.artist = artist;
    entry.genre = genre;
    entry.bpm = bpm;
    entry.key = key;
    entry.totalPlays++;
    entry.lastPlayedAt = now;

    if (userId > 0) {
        auto& users = trackUsers_[trackId];
        if (std::find(users.begin(), users.end(), userId) == users.end()) {
            users.push_back(userId);
        }
        entry.uniqueUsers = static_cast<int>(users.size());
    }

    bool foundDaily = false;
    for (auto& dp : dailyPlays_) {
        if (dp.trackId == trackId && dp.date == today) {
            dp.plays++;
            foundDaily = true;
            break;
        }
    }
    if (!foundDaily) {
        dailyPlays_.push_back({trackId, today, 1});
    }

    int64_t sevenDaysAgo = now - 7 * 86400;
    int64_t thirtyDaysAgo = now - 30 * 86400;
    entry.playsLast7Days = 0;
    entry.playsLast30Days = 0;
    for (const auto& dp : dailyPlays_) {
        if (dp.trackId != trackId) continue;
        entry.playsLast30Days += dp.plays;
        entry.playsLast7Days += dp.plays; // simplified: we count all daily entries
    }

    computeTrendScore(entry);

    spdlog::debug("TrendingTracksService: Recorded play for '{}' by {} (total: {})",
                  title, artist, entry.totalPlays);
}

void TrendingTracksService::recordPlayBatch(const std::vector<std::pair<int64_t, int64_t>>& trackUserPairs) {
    for (const auto& [trackId, userId] : trackUserPairs) {
        auto it = playData_.find(trackId);
        if (it != playData_.end()) {
            recordPlay(trackId, it->second.title, it->second.artist,
                       it->second.genre, it->second.bpm, it->second.key, userId);
        } else {
            recordPlay(trackId, "", "", "", 0.0, "", userId);
        }
    }
}


std::vector<PlayCountEntry> TrendingTracksService::getTop100(TrendingPeriod period) {
    std::lock_guard<std::mutex> lock(dataMutex_);

    std::vector<PlayCountEntry> entries;
    entries.reserve(playData_.size());
    for (const auto& [id, entry] : playData_) {
        entries.push_back(entry);
    }

    switch (period) {
        case TrendingPeriod::Daily:
            std::sort(entries.begin(), entries.end(),
                      [](const PlayCountEntry& a, const PlayCountEntry& b) {
                          return a.lastPlayedAt > b.lastPlayedAt;
                      });
            break;
        case TrendingPeriod::Weekly:
            std::sort(entries.begin(), entries.end(),
                      [](const PlayCountEntry& a, const PlayCountEntry& b) {
                          return a.playsLast7Days > b.playsLast7Days;
                      });
            break;
        case TrendingPeriod::Monthly:
            std::sort(entries.begin(), entries.end(),
                      [](const PlayCountEntry& a, const PlayCountEntry& b) {
                          return a.playsLast30Days > b.playsLast30Days;
                      });
            break;
        case TrendingPeriod::AllTime:
            std::sort(entries.begin(), entries.end(),
                      [](const PlayCountEntry& a, const PlayCountEntry& b) {
                          return a.totalPlays > b.totalPlays;
                      });
            break;
    }

    if (entries.size() > 100) entries.resize(100);
    return entries;
}

std::vector<PlayCountEntry> TrendingTracksService::getTop100ByGenre(const std::string& genre,
                                                                      TrendingPeriod period) {
    TrendingFilter filter;
    filter.genre = genre;
    filter.period = period;
    return getTop100Filtered(filter);
}

std::vector<PlayCountEntry> TrendingTracksService::getTop100Filtered(const TrendingFilter& filter) {
    std::lock_guard<std::mutex> lock(dataMutex_);

    std::vector<PlayCountEntry> entries;
    for (const auto& [id, entry] : playData_) {
        if (!filter.genre.empty() && entry.genre != filter.genre) continue;
        if (entry.bpm < filter.minBpm || entry.bpm > filter.maxBpm) continue;
        if (!filter.key.empty() && entry.key != filter.key) continue;
        entries.push_back(entry);
    }

    std::sort(entries.begin(), entries.end(),
              [](const PlayCountEntry& a, const PlayCountEntry& b) {
                  return a.trendScore > b.trendScore;
              });

    if (entries.size() > 100) entries.resize(100);
    return entries;
}


std::vector<PlayCountEntry> TrendingTracksService::getRisingTracks(int limit) {
    std::lock_guard<std::mutex> lock(dataMutex_);

    std::vector<PlayCountEntry> entries;
    for (const auto& [id, entry] : playData_) {
        if (entry.playsLast7Days > 0 && entry.totalPlays > entry.playsLast7Days) {
            double ratio = static_cast<double>(entry.playsLast7Days) /
                           static_cast<double>(entry.totalPlays);
            if (ratio > 0.3) {
                entries.push_back(entry);
            }
        }
    }

    std::sort(entries.begin(), entries.end(),
              [](const PlayCountEntry& a, const PlayCountEntry& b) {
                  return a.trendScore > b.trendScore;
              });

    if (static_cast<int>(entries.size()) > limit) entries.resize(limit);
    return entries;
}

std::vector<PlayCountEntry> TrendingTracksService::getNewTrending(int limit) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto now = nowUnix();
    int64_t sevenDaysAgo = now - 7 * 86400;

    std::vector<PlayCountEntry> entries;
    for (const auto& [id, entry] : playData_) {
        if (entry.firstPlayedAt >= sevenDaysAgo && entry.totalPlays >= 3) {
            entries.push_back(entry);
        }
    }

    std::sort(entries.begin(), entries.end(),
              [](const PlayCountEntry& a, const PlayCountEntry& b) {
                  return a.totalPlays > b.totalPlays;
              });

    if (static_cast<int>(entries.size()) > limit) entries.resize(limit);
    return entries;
}

std::vector<PlayCountEntry> TrendingTracksService::getFallingTracks(int limit) {
    std::lock_guard<std::mutex> lock(dataMutex_);

    std::vector<PlayCountEntry> entries;
    for (const auto& [id, entry] : playData_) {
        if (entry.totalPlays > 10 && entry.playsLast7Days == 0) {
            entries.push_back(entry);
        }
    }

    std::sort(entries.begin(), entries.end(),
              [](const PlayCountEntry& a, const PlayCountEntry& b) {
                  return a.totalPlays > b.totalPlays;
              });

    if (static_cast<int>(entries.size()) > limit) entries.resize(limit);
    return entries;
}

double TrendingTracksService::getTrendScore(int64_t trackId) const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto it = playData_.find(trackId);
    return (it != playData_.end()) ? it->second.trendScore : 0.0;
}


int TrendingTracksService::getTotalPlays() const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    int total = 0;
    for (const auto& [id, entry] : playData_) total += entry.totalPlays;
    return total;
}

int TrendingTracksService::getTotalUniqueTracks() const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    return static_cast<int>(playData_.size());
}

int TrendingTracksService::getTotalUniqueUsers() const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    std::vector<int64_t> allUsers;
    for (const auto& [trackId, users] : trackUsers_) {
        for (auto uid : users) {
            if (std::find(allUsers.begin(), allUsers.end(), uid) == allUsers.end()) {
                allUsers.push_back(uid);
            }
        }
    }
    return static_cast<int>(allUsers.size());
}

std::vector<std::pair<std::string, int>> TrendingTracksService::getTopGenres(int limit) const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    std::map<std::string, int> genreCounts;
    for (const auto& [id, entry] : playData_) {
        if (!entry.genre.empty()) genreCounts[entry.genre] += entry.totalPlays;
    }

    std::vector<std::pair<std::string, int>> sorted(genreCounts.begin(), genreCounts.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    if (static_cast<int>(sorted.size()) > limit) sorted.resize(limit);
    return sorted;
}

std::vector<std::pair<double, int>> TrendingTracksService::getBpmDistribution(int bucketSize) const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    std::map<int, int> buckets;
    for (const auto& [id, entry] : playData_) {
        if (entry.bpm > 0) {
            int bucket = static_cast<int>(entry.bpm / bucketSize) * bucketSize;
            buckets[bucket] += entry.totalPlays;
        }
    }

    std::vector<std::pair<double, int>> result;
    for (const auto& [bpm, count] : buckets) {
        result.emplace_back(static_cast<double>(bpm), count);
    }
    return result;
}


void TrendingTracksService::recalculateTrendScores() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    for (auto& [id, entry] : playData_) {
        computeTrendScore(entry);
    }
    spdlog::info("TrendingTracksService: Recalculated trend scores for {} tracks", playData_.size());
}

void TrendingTracksService::pruneOldData(int daysToKeep) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto now = nowUnix();
    int64_t cutoff = now - daysToKeep * 86400;

    std::vector<int64_t> toRemove;
    for (const auto& [id, entry] : playData_) {
        if (entry.lastPlayedAt < cutoff) toRemove.push_back(id);
    }
    for (auto id : toRemove) {
        playData_.erase(id);
        trackUsers_.erase(id);
    }

    dailyPlays_.erase(
        std::remove_if(dailyPlays_.begin(), dailyPlays_.end(),
                       [&toRemove](const DailyPlayCount& dp) {
                           return std::find(toRemove.begin(), toRemove.end(), dp.trackId) != toRemove.end();
                       }),
        dailyPlays_.end());

    spdlog::info("TrendingTracksService: Pruned {} old tracks (keeping {} days)", toRemove.size(), daysToKeep);
}

void TrendingTracksService::exportToJson(const std::string& filePath) const {
    std::lock_guard<std::mutex> lock(dataMutex_);

    json j = json::array();
    for (const auto& [id, entry] : playData_) {
        j.push_back({
            {"trackId", entry.trackId},
            {"title", entry.title},
            {"artist", entry.artist},
            {"genre", entry.genre},
            {"bpm", entry.bpm},
            {"key", entry.key},
            {"totalPlays", entry.totalPlays},
            {"uniqueUsers", entry.uniqueUsers},
            {"playsLast7Days", entry.playsLast7Days},
            {"playsLast30Days", entry.playsLast30Days},
            {"trendScore", entry.trendScore},
            {"lastPlayedAt", entry.lastPlayedAt},
            {"firstPlayedAt", entry.firstPlayedAt}
        });
    }

    std::ofstream ofs(filePath);
    if (ofs.is_open()) {
        ofs << j.dump(2);
        spdlog::info("TrendingTracksService: Exported {} tracks to {}", playData_.size(), filePath);
    } else {
        spdlog::error("TrendingTracksService: Failed to export to {}", filePath);
    }
}

bool TrendingTracksService::importFromJson(const std::string& filePath) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) return false;

    try {
        auto j = json::parse(ifs);
        std::lock_guard<std::mutex> lock(dataMutex_);

        for (const auto& item : j) {
            PlayCountEntry entry;
            entry.trackId = item.value("trackId", static_cast<int64_t>(0));
            entry.title = item.value("title", "");
            entry.artist = item.value("artist", "");
            entry.genre = item.value("genre", "");
            entry.bpm = item.value("bpm", 0.0);
            entry.key = item.value("key", "");
            entry.totalPlays = item.value("totalPlays", 0);
            entry.uniqueUsers = item.value("uniqueUsers", 0);
            entry.playsLast7Days = item.value("playsLast7Days", 0);
            entry.playsLast30Days = item.value("playsLast30Days", 0);
            entry.trendScore = item.value("trendScore", 0.0);
            entry.lastPlayedAt = item.value("lastPlayedAt", static_cast<int64_t>(0));
            entry.firstPlayedAt = item.value("firstPlayedAt", static_cast<int64_t>(0));
            playData_[entry.trackId] = entry;
        }

        spdlog::info("TrendingTracksService: Imported {} tracks from {}", playData_.size(), filePath);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("TrendingTracksService: Import error: {}", e.what());
        return false;
    }
}


void TrendingTracksService::registerUpdateCallback(TrendingUpdateCallback callback) {
    updateCallbacks_.push_back(std::move(callback));
}

void TrendingTracksService::notifyCallbacks() {
    auto top = getTop100(TrendingPeriod::Weekly);
    for (const auto& cb : updateCallbacks_) {
        cb(top);
    }
}


void TrendingTracksService::computeTrendScore(PlayCountEntry& entry) const {
    auto now = nowUnix();

    double daysSinceLastPlay = std::max(0.0, static_cast<double>(now - entry.lastPlayedAt) / 86400.0);
    double recency = std::exp(-daysSinceLastPlay / 7.0);

    double velocity = 0.0;
    if (entry.playsLast30Days > 0) {
        double weeklyRate = static_cast<double>(entry.playsLast7Days);
        double monthlyAvgWeekly = static_cast<double>(entry.playsLast30Days) / 4.0;
        velocity = (monthlyAvgWeekly > 0) ? std::min(weeklyRate / monthlyAvgWeekly, 3.0) / 3.0 : 0.5;
    }

    double usersScore = std::min(static_cast<double>(entry.uniqueUsers) / 50.0, 1.0);

    double playsBoost = std::log10(1.0 + entry.totalPlays) / 3.0; // normalize to ~1.0 at 1000 plays

    entry.trendScore = (recencyWeight_ * recency +
                        velocityWeight_ * velocity +
                        uniqueUsersWeight_ * usersScore) *
                       (1.0 + playsBoost);
}

}
