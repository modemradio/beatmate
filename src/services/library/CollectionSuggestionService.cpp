#include "CollectionSuggestionService.h"
#include "TrackDatabase.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <set>
#include <cmath>

namespace BeatMate::Services::Library {

CollectionSuggestionService::CollectionSuggestionService(std::shared_ptr<TrackDatabase> database)
    : database_(std::move(database)) {
}

std::vector<CollectionSuggestionItem> CollectionSuggestionService::getAllSuggestions(int maxPerType) {
    std::vector<CollectionSuggestionItem> suggestions;

    suggestions.push_back(getUnratedSuggestion(maxPerType));
    suggestions.push_back(getUnanalyzedSuggestion(maxPerType));
    suggestions.push_back(getPoorMetadataSuggestion(maxPerType));
    suggestions.push_back(getLongUnplayedSuggestion(90, maxPerType));
    suggestions.push_back(getRecentlyAddedUnplayedSuggestion(maxPerType));
    suggestions.push_back(getBPMGapSuggestion());
    suggestions.push_back(getKeyGapSuggestion());

    suggestions.erase(
        std::remove_if(suggestions.begin(), suggestions.end(),
                       [](const auto& s) { return s.affectedCount == 0; }),
        suggestions.end());

    std::sort(suggestions.begin(), suggestions.end(),
              [](const auto& a, const auto& b) { return a.priority > b.priority; });

    spdlog::info("CollectionSuggestionService: Generated {} suggestions", suggestions.size());
    return suggestions;
}

CollectionSuggestionItem CollectionSuggestionService::getUnratedSuggestion(int limit) {
    CollectionSuggestionItem item;
    item.type = SuggestionType::UnratedTracks;
    item.title = "Unrated Tracks";
    item.actionLabel = "Rate these tracks";
    if (!database_) return item;

    auto tracks = database_->getTracksByQuery(
        "SELECT * FROM tracks WHERE rating = 0 ORDER BY play_count DESC LIMIT " + std::to_string(limit));

    item.tracks = tracks;
    item.affectedCount = static_cast<int>(tracks.size());
    item.description = std::to_string(item.affectedCount) + " tracks have no rating";
    item.priority = item.affectedCount > 100 ? 0.9f : item.affectedCount > 20 ? 0.7f : 0.4f;
    return item;
}

CollectionSuggestionItem CollectionSuggestionService::getUnanalyzedSuggestion(int limit) {
    CollectionSuggestionItem item;
    item.type = SuggestionType::UnanalyzedTracks;
    item.title = "Unanalyzed Tracks";
    item.actionLabel = "Analyze now";
    if (!database_) return item;

    auto tracks = database_->getTracksByQuery(
        "SELECT * FROM tracks WHERE analyzed = 0 ORDER BY date_added DESC LIMIT " + std::to_string(limit));

    item.tracks = tracks;
    item.affectedCount = static_cast<int>(tracks.size());
    item.description = std::to_string(item.affectedCount) + " tracks need analysis (BPM, key, energy)";
    item.priority = item.affectedCount > 50 ? 0.95f : item.affectedCount > 10 ? 0.8f : 0.5f;
    return item;
}

CollectionSuggestionItem CollectionSuggestionService::getPoorMetadataSuggestion(int limit) {
    CollectionSuggestionItem item;
    item.type = SuggestionType::PoorMetadata;
    item.title = "Incomplete Metadata";
    item.actionLabel = "Edit metadata";
    if (!database_) return item;

    auto tracks = database_->getTracksByQuery(
        "SELECT * FROM tracks WHERE "
        "(title IS NULL OR title = '' OR artist IS NULL OR artist = '' OR genre IS NULL OR genre = '') "
        "ORDER BY date_added DESC LIMIT " + std::to_string(limit));

    item.tracks = tracks;
    item.affectedCount = static_cast<int>(tracks.size());
    item.description = std::to_string(item.affectedCount) + " tracks have missing title, artist, or genre";
    item.priority = item.affectedCount > 20 ? 0.8f : 0.5f;
    return item;
}

CollectionSuggestionItem CollectionSuggestionService::getLongUnplayedSuggestion(int daysSincePlay, int limit) {
    CollectionSuggestionItem item;
    item.type = SuggestionType::LongUnplayed;
    item.title = "Forgotten Tracks";
    item.actionLabel = "Rediscover";
    if (!database_) return item;

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto threshold = now - (daysSincePlay * 86400);

    auto tracks = database_->getTracksByQuery(
        "SELECT * FROM tracks WHERE play_count > 0 AND last_played > 0 AND last_played < " +
        std::to_string(threshold) +
        " AND rating >= 3 ORDER BY rating DESC, play_count DESC LIMIT " + std::to_string(limit));

    item.tracks = tracks;
    item.affectedCount = static_cast<int>(tracks.size());
    item.description = std::to_string(item.affectedCount) + " good tracks haven't been played in " +
                       std::to_string(daysSincePlay) + " days";
    item.priority = 0.6f;
    return item;
}

CollectionSuggestionItem CollectionSuggestionService::getRecentlyAddedUnplayedSuggestion(int limit) {
    CollectionSuggestionItem item;
    item.type = SuggestionType::RecentlyAdded;
    item.title = "New & Unplayed";
    item.actionLabel = "Listen now";
    if (!database_) return item;

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto threshold = now - (30 * 86400); // Last 30 days

    auto tracks = database_->getTracksByQuery(
        "SELECT * FROM tracks WHERE play_count = 0 AND date_added > " +
        std::to_string(threshold) +
        " ORDER BY date_added DESC LIMIT " + std::to_string(limit));

    item.tracks = tracks;
    item.affectedCount = static_cast<int>(tracks.size());
    item.description = std::to_string(item.affectedCount) + " recently added tracks haven't been played yet";
    item.priority = 0.7f;
    return item;
}

CollectionSuggestionItem CollectionSuggestionService::getBPMGapSuggestion() {
    CollectionSuggestionItem item;
    item.type = SuggestionType::BPMGap;
    item.title = "BPM Coverage Gaps";
    item.actionLabel = "View gaps";
    if (!database_) return item;

    auto allTracks = database_->getAllTracks();

    std::map<int, int> bpmBuckets;
    for (const auto& t : allTracks) {
        if (t.bpm > 0) {
            int bucket = (static_cast<int>(t.bpm) / 10) * 10;
            bpmBuckets[bucket]++;
        }
    }

    std::vector<std::string> gaps;
    for (int bpm = 70; bpm <= 180; bpm += 10) {
        int count = bpmBuckets.count(bpm) > 0 ? bpmBuckets[bpm] : 0;
        if (count < 3) {
            gaps.push_back(std::to_string(bpm) + "-" + std::to_string(bpm + 10) +
                          " BPM (" + std::to_string(count) + " tracks)");
        }
    }

    item.affectedCount = static_cast<int>(gaps.size());
    item.description = std::to_string(gaps.size()) + " BPM ranges have very few tracks";
    item.priority = gaps.size() > 5 ? 0.5f : 0.3f;
    return item;
}

CollectionSuggestionItem CollectionSuggestionService::getKeyGapSuggestion() {
    CollectionSuggestionItem item;
    item.type = SuggestionType::KeyGap;
    item.title = "Key Coverage Gaps";
    item.actionLabel = "View gaps";
    if (!database_) return item;

    auto allTracks = database_->getAllTracks();

    std::map<std::string, int> keyCounts;
    for (const auto& t : allTracks) {
        std::string k = !t.camelotKey.empty() ? t.camelotKey : t.key;
        if (!k.empty()) keyCounts[k]++;
    }

    std::vector<std::string> allKeys;
    for (int i = 1; i <= 12; ++i) {
        allKeys.push_back(std::to_string(i) + "A");
        allKeys.push_back(std::to_string(i) + "B");
    }

    std::vector<std::string> gaps;
    for (const auto& key : allKeys) {
        int count = keyCounts.count(key) > 0 ? keyCounts[key] : 0;
        if (count < 3) {
            gaps.push_back(key + " (" + std::to_string(count) + " tracks)");
        }
    }

    item.affectedCount = static_cast<int>(gaps.size());
    item.description = std::to_string(gaps.size()) + " keys have very few tracks";
    item.priority = gaps.size() > 10 ? 0.5f : 0.3f;
    return item;
}

CollectionHealth CollectionSuggestionService::analyzeCollectionHealth() {
    CollectionHealth health;
    if (!database_) return health;

    auto allTracks = database_->getAllTracks();
    health.totalTracks = static_cast<int>(allTracks.size());

    if (health.totalTracks == 0) {
        health.overallGrade = "N/A";
        return health;
    }

    for (const auto& t : allTracks) {
        if (t.analyzed) health.analyzedTracks++;
        if (t.rating > 0) health.ratedTracks++;
        if (t.bpm > 0) health.tracksWithBPM++;
        if (!t.key.empty() || !t.camelotKey.empty()) health.tracksWithKey++;
        if (!t.genre.empty()) health.tracksWithGenre++;
        if (!t.mood.empty()) health.tracksWithMood++;
        if (t.energy > 0) health.tracksWithEnergy++;
        if (t.playCount == 0) health.neverPlayedTracks++;
    }

    float total = static_cast<float>(health.totalTracks);

    health.completenessScore =
        (health.tracksWithBPM / total * 20.0f) +
        (health.tracksWithKey / total * 15.0f) +
        (health.tracksWithGenre / total * 15.0f) +
        (health.analyzedTracks / total * 20.0f) +
        (health.ratedTracks / total * 10.0f) +
        (health.tracksWithEnergy / total * 10.0f) +
        (health.tracksWithMood / total * 10.0f);

    health.qualityScore =
        (health.analyzedTracks / total * 30.0f) +
        (health.ratedTracks / total * 20.0f) +
        (health.tracksWithBPM / total * 20.0f) +
        (health.tracksWithKey / total * 15.0f) +
        (health.tracksWithGenre / total * 15.0f);

    float avgScore = (health.completenessScore + health.qualityScore) / 2.0f;
    health.overallGrade = gradeFromScore(avgScore);

    spdlog::info("CollectionSuggestionService: Health analysis - {} tracks, completeness={:.1f}%, quality={:.1f}%, grade={}",
                 health.totalTracks, health.completenessScore, health.qualityScore, health.overallGrade);

    return health;
}

std::map<std::string, int> CollectionSuggestionService::getGenreDistribution() const {
    if (!database_) return {};

    auto allTracks = database_->getAllTracks();
    std::map<std::string, int> dist;
    for (const auto& t : allTracks) {
        if (!t.genre.empty()) dist[t.genre]++;
    }
    return dist;
}

std::vector<std::string> CollectionSuggestionService::getUnderrepresentedGenres(int threshold) const {
    auto dist = getGenreDistribution();
    std::vector<std::string> result;
    for (const auto& [genre, count] : dist) {
        if (count < threshold) result.push_back(genre);
    }
    return result;
}

std::vector<std::string> CollectionSuggestionService::getTopGenres(int count) const {
    auto dist = getGenreDistribution();
    std::vector<std::pair<std::string, int>> sorted(dist.begin(), dist.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    std::vector<std::string> result;
    for (size_t i = 0; i < std::min(sorted.size(), static_cast<size_t>(count)); ++i) {
        result.push_back(sorted[i].first);
    }
    return result;
}

std::vector<Models::Track> CollectionSuggestionService::getDiscoveryPicks(int count) {
    if (!database_) return {};

    return database_->getTracksByQuery(
        "SELECT * FROM tracks WHERE play_count <= 2 AND rating >= 3 "
        "ORDER BY RANDOM() LIMIT " + std::to_string(count));
}

std::vector<Models::Track> CollectionSuggestionService::getRediscoveryPicks(int count) {
    if (!database_) return {};

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto threshold = now - (180 * 86400); // 6 months ago

    return database_->getTracksByQuery(
        "SELECT * FROM tracks WHERE play_count > 3 AND last_played < " +
        std::to_string(threshold) +
        " AND rating >= 3 ORDER BY rating DESC LIMIT " + std::to_string(count));
}

std::vector<Models::Track> CollectionSuggestionService::getFavoritePredictions(int count) {
    if (!database_) return {};

    return database_->getTracksByQuery(
        "SELECT * FROM tracks WHERE rating = 0 AND analyzed = 1 AND play_count > 0 "
        "ORDER BY play_count DESC LIMIT " + std::to_string(count));
}

std::string CollectionSuggestionService::gradeFromScore(float score) const {
    if (score >= 90.0f) return "A+";
    if (score >= 80.0f) return "A";
    if (score >= 70.0f) return "B";
    if (score >= 60.0f) return "C";
    if (score >= 50.0f) return "D";
    return "F";
}

} // namespace BeatMate::Services::Library
