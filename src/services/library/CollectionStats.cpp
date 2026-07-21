#include "CollectionStats.h"
#include "TrackDatabase.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <spdlog/spdlog.h>

namespace BeatMate::Services::Library {

CollectionStats::CollectionStats(std::shared_ptr<TrackDatabase> database)
    : database_(std::move(database)) {
}

int64_t CollectionStats::getTotalTracks() {
    return database_->getTrackCount();
}

double CollectionStats::getTotalDuration() {
    auto tracks = database_->getAllTracks();
    double total = 0.0;
    for (const auto& t : tracks) total += t.duration;
    return total / 3600.0;
}

int64_t CollectionStats::getTotalSize() {
    auto tracks = database_->getAllTracks();
    int64_t total = 0;
    for (const auto& t : tracks) total += t.fileSize;
    return total;
}

std::vector<StatEntry> CollectionStats::getGenreDistribution() {
    auto tracks = database_->getAllTracks();
    std::map<std::string, int> counts;
    for (const auto& t : tracks) {
        if (!t.genre.empty()) counts[t.genre]++;
    }

    int total = static_cast<int>(tracks.size());
    std::vector<StatEntry> entries;
    for (const auto& [genre, count] : counts) {
        entries.push_back({genre, count, total > 0 ? static_cast<float>(count) / total * 100.0f : 0.0f});
    }

    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.count > b.count; });
    return entries;
}

std::vector<StatEntry> CollectionStats::getBPMDistribution(double step) {
    auto tracks = database_->getAllTracks();
    std::map<int, int> counts;
    for (const auto& t : tracks) {
        if (t.bpm > 0) {
            int bucket = static_cast<int>(std::floor(t.bpm / step) * step);
            counts[bucket]++;
        }
    }

    int total = static_cast<int>(tracks.size());
    std::vector<StatEntry> entries;
    for (const auto& [bpm, count] : counts) {
        std::string label = std::to_string(bpm) + "-" + std::to_string(bpm + static_cast<int>(step));
        entries.push_back({label, count, total > 0 ? static_cast<float>(count) / total * 100.0f : 0.0f});
    }

    return entries;
}

std::vector<StatEntry> CollectionStats::getKeyDistribution() {
    auto tracks = database_->getAllTracks();
    std::map<std::string, int> counts;
    for (const auto& t : tracks) {
        std::string k = !t.camelotKey.empty() ? t.camelotKey : t.key;
        if (!k.empty()) counts[k]++;
    }

    int total = static_cast<int>(tracks.size());
    std::vector<StatEntry> entries;
    for (const auto& [key, count] : counts) {
        entries.push_back({key, count, total > 0 ? static_cast<float>(count) / total * 100.0f : 0.0f});
    }

    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.count > b.count; });
    return entries;
}

std::vector<StatEntry> CollectionStats::getFormatDistribution() {
    auto tracks = database_->getAllTracks();
    std::map<std::string, int> counts;
    for (const auto& t : tracks) {
        if (!t.fileFormat.empty()) counts[t.fileFormat]++;
    }

    int total = static_cast<int>(tracks.size());
    std::vector<StatEntry> entries;
    for (const auto& [format, count] : counts) {
        entries.push_back({format, count, total > 0 ? static_cast<float>(count) / total * 100.0f : 0.0f});
    }

    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.count > b.count; });
    return entries;
}

std::vector<StatEntry> CollectionStats::getYearDistribution() {
    auto tracks = database_->getAllTracks();
    std::map<int, int> counts;
    for (const auto& t : tracks) {
        if (t.year > 0) counts[t.year]++;
    }

    int total = static_cast<int>(tracks.size());
    std::vector<StatEntry> entries;
    for (const auto& [year, count] : counts) {
        entries.push_back({std::to_string(year), count, total > 0 ? static_cast<float>(count) / total * 100.0f : 0.0f});
    }

    return entries;
}

std::vector<StatEntry> CollectionStats::getRatingDistribution() {
    auto tracks = database_->getAllTracks();
    std::map<int, int> counts;
    for (const auto& t : tracks) counts[t.rating]++;

    int total = static_cast<int>(tracks.size());
    std::vector<StatEntry> entries;
    for (const auto& [rating, count] : counts) {
        entries.push_back({std::to_string(rating) + " stars", count, total > 0 ? static_cast<float>(count) / total * 100.0f : 0.0f});
    }

    return entries;
}

std::vector<StatEntry> CollectionStats::getEnergyDistribution(float step) {
    auto tracks = database_->getAllTracks();
    std::map<int, int> counts;
    for (const auto& t : tracks) {
        if (t.energy > 0) {
            int bucket = static_cast<int>(std::floor(t.energy / step) * step);
            counts[bucket]++;
        }
    }

    int total = static_cast<int>(tracks.size());
    std::vector<StatEntry> entries;
    for (const auto& [energy, count] : counts) {
        entries.push_back({std::to_string(energy), count, total > 0 ? static_cast<float>(count) / total * 100.0f : 0.0f});
    }

    return entries;
}

std::vector<StatEntry> CollectionStats::getSourceDistribution() {
    auto tracks = database_->getAllTracks();
    std::map<int, int> counts;
    for (const auto& t : tracks) counts[static_cast<int>(t.source)]++;

    int total = static_cast<int>(tracks.size());
    std::vector<StatEntry> entries;
    static const char* sourceNames[] = {"Local", "VirtualDJ", "Rekordbox", "Serato", "Traktor", "EngineDJ", "Streaming"};
    for (const auto& [src, count] : counts) {
        std::string name = (src >= 0 && src < 7) ? sourceNames[src] : "Unknown";
        entries.push_back({name, count, total > 0 ? static_cast<float>(count) / total * 100.0f : 0.0f});
    }

    return entries;
}


std::vector<StatEntry> CollectionStats::getMoodDistribution() {
    auto tracks = database_->getAllTracks();
    std::map<std::string, int> counts;
    for (const auto& t : tracks) {
        if (!t.mood.empty()) counts[t.mood]++;
    }

    int total = static_cast<int>(tracks.size());
    std::vector<StatEntry> entries;
    for (const auto& [mood, count] : counts) {
        entries.push_back({mood, count, total > 0 ? static_cast<float>(count) / total * 100.0f : 0.0f});
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.count > b.count; });
    return entries;
}

std::vector<StatEntry> CollectionStats::getDanceabilityDistribution(float step) {
    auto tracks = database_->getAllTracks();
    std::map<int, int> counts;
    for (const auto& t : tracks) {
        if (t.danceability > 0) {
            int bucket = static_cast<int>(std::floor(t.danceability / step) * 10);
            counts[bucket]++;
        }
    }

    int total = static_cast<int>(tracks.size());
    std::vector<StatEntry> entries;
    for (const auto& [bucket, count] : counts) {
        std::ostringstream label;
        label << std::fixed << std::setprecision(1) << (bucket * step) << "-" << ((bucket + 1) * step);
        entries.push_back({label.str(), count, total > 0 ? static_cast<float>(count) / total * 100.0f : 0.0f});
    }
    return entries;
}

std::vector<StatEntry> CollectionStats::getBitRateDistribution() {
    auto tracks = database_->getAllTracks();
    std::map<int, int> counts;
    for (const auto& t : tracks) {
        if (t.bitRate > 0) {
            int bucket = (t.bitRate / 32) * 32;
            counts[bucket]++;
        }
    }

    int total = static_cast<int>(tracks.size());
    std::vector<StatEntry> entries;
    for (const auto& [br, count] : counts) {
        entries.push_back({std::to_string(br) + " kbps", count,
                           total > 0 ? static_cast<float>(count) / total * 100.0f : 0.0f});
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.count > b.count; });
    return entries;
}

std::vector<StatEntry> CollectionStats::getSampleRateDistribution() {
    auto tracks = database_->getAllTracks();
    std::map<int, int> counts;
    for (const auto& t : tracks) {
        if (t.sampleRate > 0) counts[t.sampleRate]++;
    }

    int total = static_cast<int>(tracks.size());
    std::vector<StatEntry> entries;
    for (const auto& [sr, count] : counts) {
        std::string label = std::to_string(sr / 1000) + "." + std::to_string((sr % 1000) / 100) + " kHz";
        entries.push_back({label, count, total > 0 ? static_cast<float>(count) / total * 100.0f : 0.0f});
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.count > b.count; });
    return entries;
}

std::vector<StatEntry> CollectionStats::getTopArtists(int limit) {
    auto tracks = database_->getAllTracks();
    std::map<std::string, int> counts;
    for (const auto& t : tracks) {
        if (!t.artist.empty()) counts[t.artist]++;
    }

    int total = static_cast<int>(tracks.size());
    std::vector<StatEntry> entries;
    for (const auto& [artist, count] : counts) {
        entries.push_back({artist, count, total > 0 ? static_cast<float>(count) / total * 100.0f : 0.0f});
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.count > b.count; });
    if (static_cast<int>(entries.size()) > limit) entries.resize(static_cast<size_t>(limit));
    return entries;
}

std::vector<StatEntry> CollectionStats::getTopAlbums(int limit) {
    auto tracks = database_->getAllTracks();
    std::map<std::string, int> counts;
    for (const auto& t : tracks) {
        if (!t.album.empty()) counts[t.album]++;
    }

    int total = static_cast<int>(tracks.size());
    std::vector<StatEntry> entries;
    for (const auto& [album, count] : counts) {
        entries.push_back({album, count, total > 0 ? static_cast<float>(count) / total * 100.0f : 0.0f});
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.count > b.count; });
    if (static_cast<int>(entries.size()) > limit) entries.resize(static_cast<size_t>(limit));
    return entries;
}

std::vector<StatEntry> CollectionStats::getAddedByMonth() {
    auto tracks = database_->getAllTracks();
    std::map<std::string, int> counts;

    for (const auto& t : tracks) {
        if (t.dateAdded > 0) {
            auto tp = std::chrono::system_clock::from_time_t(static_cast<time_t>(t.dateAdded));
            auto tt = std::chrono::system_clock::to_time_t(tp);
            std::tm tm = {};
#ifdef _WIN32
            localtime_s(&tm, &tt);
#else
            localtime_r(&tt, &tm);
#endif
            std::ostringstream label;
            label << std::setfill('0') << (tm.tm_year + 1900) << "-" << std::setw(2) << (tm.tm_mon + 1);
            counts[label.str()]++;
        }
    }

    std::vector<StatEntry> entries;
    int total = static_cast<int>(tracks.size());
    for (const auto& [month, count] : counts) {
        entries.push_back({month, count, total > 0 ? static_cast<float>(count) / total * 100.0f : 0.0f});
    }
    return entries;
}

std::vector<StatEntry> CollectionStats::getPlaysByDayOfWeek() {
    auto tracks = database_->getAllTracks();
    std::map<int, int> counts;
    static const char* dayNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

    for (const auto& t : tracks) {
        if (t.lastPlayed > 0) {
            auto tp = std::chrono::system_clock::from_time_t(static_cast<time_t>(t.lastPlayed));
            auto tt = std::chrono::system_clock::to_time_t(tp);
            std::tm tm = {};
#ifdef _WIN32
            localtime_s(&tm, &tt);
#else
            localtime_r(&tt, &tm);
#endif
            counts[tm.tm_wday] += t.playCount;
        }
    }

    std::vector<StatEntry> entries;
    int total = 0;
    for (const auto& [_, count] : counts) total += count;

    for (int day = 0; day < 7; ++day) {
        int count = counts.count(day) > 0 ? counts[day] : 0;
        entries.push_back({dayNames[day], count, total > 0 ? static_cast<float>(count) / total * 100.0f : 0.0f});
    }
    return entries;
}


double CollectionStats::getAverageBPM() {
    auto tracks = database_->getAllTracks();
    double sum = 0; int count = 0;
    for (const auto& t : tracks) { if (t.bpm > 0) { sum += t.bpm; count++; } }
    return count > 0 ? sum / count : 0.0;
}

double CollectionStats::getAverageDuration() {
    auto tracks = database_->getAllTracks();
    double sum = 0; int count = 0;
    for (const auto& t : tracks) { if (t.duration > 0) { sum += t.duration; count++; } }
    return count > 0 ? sum / count : 0.0;
}

float CollectionStats::getAverageEnergy() {
    auto tracks = database_->getAllTracks();
    float sum = 0; int count = 0;
    for (const auto& t : tracks) { if (t.energy > 0) { sum += t.energy; count++; } }
    return count > 0 ? sum / count : 0.0f;
}

float CollectionStats::getAverageRating() {
    auto tracks = database_->getAllTracks();
    float sum = 0; int count = 0;
    for (const auto& t : tracks) { if (t.rating > 0) { sum += t.rating; count++; } }
    return count > 0 ? sum / count : 0.0f;
}

float CollectionStats::getAverageDanceability() {
    auto tracks = database_->getAllTracks();
    float sum = 0; int count = 0;
    for (const auto& t : tracks) { if (t.danceability > 0) { sum += t.danceability; count++; } }
    return count > 0 ? sum / count : 0.0f;
}

int CollectionStats::getAverageBitRate() {
    auto tracks = database_->getAllTracks();
    int64_t sum = 0; int count = 0;
    for (const auto& t : tracks) { if (t.bitRate > 0) { sum += t.bitRate; count++; } }
    return count > 0 ? static_cast<int>(sum / count) : 0;
}


double CollectionStats::getMinBPM() {
    auto tracks = database_->getAllTracks();
    double minBPM = 999.0;
    for (const auto& t : tracks) { if (t.bpm > 0) minBPM = std::min(minBPM, t.bpm); }
    return minBPM < 999.0 ? minBPM : 0.0;
}

double CollectionStats::getMaxBPM() {
    auto tracks = database_->getAllTracks();
    double maxBPM = 0.0;
    for (const auto& t : tracks) { if (t.bpm > 0) maxBPM = std::max(maxBPM, t.bpm); }
    return maxBPM;
}


int CollectionStats::getAnalyzedCount() {
    auto tracks = database_->getAllTracks();
    int count = 0;
    for (const auto& t : tracks) { if (t.analyzed) count++; }
    return count;
}

int CollectionStats::getRatedCount() {
    auto tracks = database_->getAllTracks();
    int count = 0;
    for (const auto& t : tracks) { if (t.rating > 0) count++; }
    return count;
}

int CollectionStats::getTracksWithBPM() {
    auto tracks = database_->getAllTracks();
    int count = 0;
    for (const auto& t : tracks) { if (t.bpm > 0) count++; }
    return count;
}

int CollectionStats::getTracksWithKey() {
    auto tracks = database_->getAllTracks();
    int count = 0;
    for (const auto& t : tracks) { if (!t.key.empty() || !t.camelotKey.empty()) count++; }
    return count;
}

int CollectionStats::getTracksWithGenre() {
    auto tracks = database_->getAllTracks();
    int count = 0;
    for (const auto& t : tracks) { if (!t.genre.empty()) count++; }
    return count;
}

int CollectionStats::getTracksWithMood() {
    auto tracks = database_->getAllTracks();
    int count = 0;
    for (const auto& t : tracks) { if (!t.mood.empty()) count++; }
    return count;
}

int CollectionStats::getNeverPlayedCount() {
    auto tracks = database_->getAllTracks();
    int count = 0;
    for (const auto& t : tracks) { if (t.playCount == 0) count++; }
    return count;
}

float CollectionStats::getMetadataCompleteness() {
    auto tracks = database_->getAllTracks();
    if (tracks.empty()) return 0.0f;

    int total = static_cast<int>(tracks.size());
    int fields = 0;
    int filled = 0;

    for (const auto& t : tracks) {
        fields += 8;
        if (!t.title.empty()) filled++;
        if (!t.artist.empty()) filled++;
        if (!t.album.empty()) filled++;
        if (!t.genre.empty()) filled++;
        if (t.bpm > 0) filled++;
        if (!t.key.empty() || !t.camelotKey.empty()) filled++;
        if (t.energy > 0) filled++;
        if (t.year > 0) filled++;
    }

    return fields > 0 ? static_cast<float>(filled) / fields * 100.0f : 0.0f;
}

CollectionReport CollectionStats::generateFullReport() {
    CollectionReport report;

    report.totalTracks = getTotalTracks();
    report.totalDurationHours = getTotalDuration();
    report.totalSizeBytes = getTotalSize();
    report.totalSizeFormatted = formatBytes(report.totalSizeBytes);

    report.averageBPM = getAverageBPM();
    report.averageDuration = getAverageDuration();
    report.averageEnergy = getAverageEnergy();
    report.averageRating = getAverageRating();
    report.averageDanceability = getAverageDanceability();
    report.averageBitRate = getAverageBitRate();

    report.minBPM = getMinBPM();
    report.maxBPM = getMaxBPM();

    report.analyzedCount = getAnalyzedCount();
    report.unanalyzedCount = static_cast<int>(report.totalTracks) - report.analyzedCount;
    report.ratedCount = getRatedCount();
    report.unratedCount = static_cast<int>(report.totalTracks) - report.ratedCount;
    report.withBPM = getTracksWithBPM();
    report.withKey = getTracksWithKey();
    report.withGenre = getTracksWithGenre();
    report.withMood = getTracksWithMood();
    report.neverPlayed = getNeverPlayedCount();
    report.playedAtLeastOnce = static_cast<int>(report.totalTracks) - report.neverPlayed;

    report.genreDistribution = getGenreDistribution();
    report.bpmDistribution = getBPMDistribution();
    report.keyDistribution = getKeyDistribution();
    report.formatDistribution = getFormatDistribution();
    report.yearDistribution = getYearDistribution();
    report.ratingDistribution = getRatingDistribution();
    report.energyDistribution = getEnergyDistribution();
    report.sourceDistribution = getSourceDistribution();
    report.moodDistribution = getMoodDistribution();
    report.danceabilityDistribution = getDanceabilityDistribution();
    report.bitRateDistribution = getBitRateDistribution();
    report.sampleRateDistribution = getSampleRateDistribution();

    report.topArtists = getTopArtists();
    report.topAlbums = getTopAlbums();
    report.addedByMonth = getAddedByMonth();
    report.playsByDayOfWeek = getPlaysByDayOfWeek();

    report.metadataCompleteness = getMetadataCompleteness();

    spdlog::info("CollectionStats: Full report generated - {} tracks, {:.1f}h, {}, completeness={:.1f}%",
                 report.totalTracks, report.totalDurationHours,
                 report.totalSizeFormatted, report.metadataCompleteness);

    return report;
}

std::string CollectionStats::formatBytes(int64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = static_cast<double>(bytes);
    while (size >= 1024.0 && unit < 4) {
        size /= 1024.0;
        unit++;
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(unit > 0 ? 1 : 0) << size << " " << units[unit];
    return ss.str();
}

std::string CollectionStats::formatDuration(double seconds) {
    int hours = static_cast<int>(seconds / 3600.0);
    int minutes = static_cast<int>(std::fmod(seconds, 3600.0) / 60.0);
    int secs = static_cast<int>(std::fmod(seconds, 60.0));

    std::ostringstream ss;
    if (hours > 0) ss << hours << "h ";
    ss << minutes << "m " << secs << "s";
    return ss.str();
}

}
