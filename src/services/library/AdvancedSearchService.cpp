#include "AdvancedSearchService.h"
#include "TrackDatabase.h"

#include <spdlog/spdlog.h>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <map>

namespace BeatMate::Services::Library {

static const std::map<std::string, std::vector<std::string>>& getCamelotCompatMap() {
    static const std::map<std::string, std::vector<std::string>> compat = {
        {"1A",  {"1A", "12A", "2A", "1B"}},   {"1B",  {"1B", "12B", "2B", "1A"}},
        {"2A",  {"2A", "1A",  "3A", "2B"}},   {"2B",  {"2B", "1B",  "3B", "2A"}},
        {"3A",  {"3A", "2A",  "4A", "3B"}},   {"3B",  {"3B", "2B",  "4B", "3A"}},
        {"4A",  {"4A", "3A",  "5A", "4B"}},   {"4B",  {"4B", "3B",  "5B", "4A"}},
        {"5A",  {"5A", "4A",  "6A", "5B"}},   {"5B",  {"5B", "4B",  "6B", "5A"}},
        {"6A",  {"6A", "5A",  "7A", "6B"}},   {"6B",  {"6B", "5B",  "7B", "6A"}},
        {"7A",  {"7A", "6A",  "8A", "7B"}},   {"7B",  {"7B", "6B",  "8B", "7A"}},
        {"8A",  {"8A", "7A",  "9A", "8B"}},   {"8B",  {"8B", "7B",  "9B", "8A"}},
        {"9A",  {"9A", "8A",  "10A","9B"}},   {"9B",  {"9B", "8B",  "10B","9A"}},
        {"10A", {"10A","9A",  "11A","10B"}},   {"10B", {"10B","9B",  "11B","10A"}},
        {"11A", {"11A","10A", "12A","11B"}},   {"11B", {"11B","10B", "12B","11A"}},
        {"12A", {"12A","11A", "1A", "12B"}},   {"12B", {"12B","11B", "1B", "12A"}},
    };
    return compat;
}

AdvancedSearchService::AdvancedSearchService(std::shared_ptr<TrackDatabase> database)
    : database_(std::move(database)) {
}

std::vector<AdvancedSearchResult> AdvancedSearchService::search(const AdvancedFilter& filter) {
    if (!database_) return {};

    std::vector<std::string> params;
    std::string sql = buildSQL(filter, params);
    spdlog::debug("AdvancedSearchService: SQL = {}", sql);

    auto tracks = database_->getTracksByQuery(sql, params);

    std::vector<AdvancedSearchResult> results;
    results.reserve(tracks.size());

    for (auto& track : tracks) {
        AdvancedSearchResult result;
        result.track = std::move(track);
        result.score = calculateRelevanceScore(result.track, filter);
        result.matchedFields = determineMatchedFields(result.track, filter);
        results.push_back(std::move(result));
    }

    if (filter.sortBy == "relevance") {
        std::sort(results.begin(), results.end(),
                  [](const auto& a, const auto& b) { return a.score > b.score; });
    }

    if (!filter.query.empty()) {
        addRecentSearch(filter.query);
    }

    spdlog::debug("AdvancedSearchService: Found {} results", results.size());
    return results;
}

std::vector<AdvancedSearchResult> AdvancedSearchService::searchByBPMRange(double minBPM, double maxBPM, int limit) {
    AdvancedFilter filter;
    filter.bpmMin = minBPM;
    filter.bpmMax = maxBPM;
    filter.sortBy = "bpm";
    filter.ascending = true;
    filter.limit = limit;
    return search(filter);
}

std::vector<AdvancedSearchResult> AdvancedSearchService::searchByKey(const std::string& key, bool includeCompatible, int limit) {
    AdvancedFilter filter;
    filter.key = key;
    filter.includeCompatibleKeys = includeCompatible;
    filter.limit = limit;
    return search(filter);
}

std::vector<AdvancedSearchResult> AdvancedSearchService::searchByGenre(const std::string& genre, int limit) {
    AdvancedFilter filter;
    filter.genre = genre;
    filter.limit = limit;
    return search(filter);
}

std::vector<AdvancedSearchResult> AdvancedSearchService::searchByEnergyRange(float minE, float maxE, int limit) {
    AdvancedFilter filter;
    filter.energyMin = minE;
    filter.energyMax = maxE;
    filter.sortBy = "energy";
    filter.ascending = true;
    filter.limit = limit;
    return search(filter);
}

std::vector<AdvancedSearchResult> AdvancedSearchService::searchByMood(const std::string& mood, int limit) {
    AdvancedFilter filter;
    filter.mood = mood;
    filter.limit = limit;
    return search(filter);
}

std::vector<AdvancedSearchResult> AdvancedSearchService::searchRecentlyAdded(int days, int limit) {
    AdvancedFilter filter;
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    filter.addedAfter = now - (days * 86400);
    filter.sortBy = "date_added";
    filter.ascending = false;
    filter.limit = limit;
    return search(filter);
}

std::vector<AdvancedSearchResult> AdvancedSearchService::searchNeverPlayed(int limit) {
    AdvancedFilter filter;
    filter.neverPlayed = true;
    filter.sortBy = "date_added";
    filter.ascending = false;
    filter.limit = limit;
    return search(filter);
}

std::vector<AdvancedSearchResult> AdvancedSearchService::searchMostPlayed(int limit) {
    AdvancedFilter filter;
    filter.playCountMin = 1;
    filter.sortBy = "play_count";
    filter.ascending = false;
    filter.limit = limit;
    return search(filter);
}

std::vector<AdvancedSearchResult> AdvancedSearchService::searchHighRated(int minRating, int limit) {
    AdvancedFilter filter;
    filter.ratingMin = minRating;
    filter.sortBy = "rating";
    filter.ascending = false;
    filter.limit = limit;
    return search(filter);
}

std::vector<AdvancedSearchResult> AdvancedSearchService::searchUnanalyzed(int limit) {
    AdvancedFilter filter;
    filter.analyzed = false;
    filter.limit = limit;
    return search(filter);
}

std::vector<std::string> AdvancedSearchService::getCompatibleKeys(const std::string& key) {
    auto& compat = getCamelotCompatMap();
    auto it = compat.find(key);
    if (it != compat.end()) return it->second;
    return {key};
}

bool AdvancedSearchService::areKeysCompatible(const std::string& key1, const std::string& key2) {
    auto compatible = getCompatibleKeys(key1);
    return std::find(compatible.begin(), compatible.end(), key2) != compatible.end();
}

void AdvancedSearchService::savePreset(const SearchPreset& preset) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(presets_.begin(), presets_.end(),
                            [&](const auto& p) { return p.name == preset.name; });
    if (it != presets_.end()) {
        *it = preset;
    } else {
        presets_.push_back(preset);
    }
    spdlog::info("AdvancedSearchService: Saved preset '{}'", preset.name);
}

std::vector<SearchPreset> AdvancedSearchService::getPresets() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return presets_;
}

void AdvancedSearchService::deletePreset(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    presets_.erase(std::remove_if(presets_.begin(), presets_.end(),
                                   [&](const auto& p) { return p.name == name; }),
                   presets_.end());
}

std::vector<std::string> AdvancedSearchService::getRecentSearches(int limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto count = std::min(static_cast<int>(recentSearches_.size()), limit);
    return std::vector<std::string>(recentSearches_.begin(), recentSearches_.begin() + count);
}

void AdvancedSearchService::addRecentSearch(const std::string& query) {
    std::lock_guard<std::mutex> lock(mutex_);
    recentSearches_.erase(std::remove(recentSearches_.begin(), recentSearches_.end(), query),
                           recentSearches_.end());
    recentSearches_.insert(recentSearches_.begin(), query);
    if (recentSearches_.size() > 50) recentSearches_.resize(50);
}

void AdvancedSearchService::clearRecentSearches() {
    std::lock_guard<std::mutex> lock(mutex_);
    recentSearches_.clear();
}

std::string AdvancedSearchService::buildSQL(const AdvancedFilter& filter,
                                            std::vector<std::string>& params) const {
    std::ostringstream sql;
    sql << "SELECT * FROM tracks WHERE 1=1";

    if (!filter.query.empty()) {
        sql << " AND (title LIKE ? OR artist LIKE ? OR album LIKE ? OR comment LIKE ?)";
        const std::string like = "%" + filter.query + "%";
        params.push_back(like);
        params.push_back(like);
        params.push_back(like);
        params.push_back(like);
    }
    if (filter.titleFilter)  { sql << " AND title LIKE ?";  params.push_back("%" + *filter.titleFilter + "%"); }
    if (filter.artistFilter) { sql << " AND artist LIKE ?"; params.push_back("%" + *filter.artistFilter + "%"); }
    if (filter.albumFilter)  { sql << " AND album LIKE ?";  params.push_back("%" + *filter.albumFilter + "%"); }

    if (filter.bpmMin) {
        double minBPM = *filter.bpmMin - filter.bpmTolerance;
        sql << " AND bpm >= " << minBPM;
    }
    if (filter.bpmMax) {
        double maxBPM = *filter.bpmMax + filter.bpmTolerance;
        sql << " AND bpm <= " << maxBPM;
    }

    if (filter.key) {
        if (filter.includeCompatibleKeys) {
            auto keys = getCompatibleKeys(*filter.key);
            sql << " AND (";
            for (size_t i = 0; i < keys.size(); ++i) {
                if (i > 0) sql << " OR ";
                sql << "key = ? OR camelot_key = ?";
                params.push_back(keys[i]);
                params.push_back(keys[i]);
            }
            sql << ")";
        } else {
            sql << " AND (key = ? OR camelot_key = ? OR open_key = ?)";
            params.push_back(*filter.key);
            params.push_back(*filter.key);
            params.push_back(*filter.key);
        }
    }

    if (filter.genre) {
        sql << " AND genre LIKE ?";
        params.push_back("%" + *filter.genre + "%");
    }
    if (!filter.genres.empty()) {
        sql << " AND (";
        bool first = true;
        for (const auto& g : filter.genres) {
            if (!first) sql << " OR ";
            sql << "genre LIKE ?";
            params.push_back("%" + g + "%");
            first = false;
        }
        sql << ")";
    }

    if (filter.energyMin) sql << " AND energy >= " << *filter.energyMin;
    if (filter.energyMax) sql << " AND energy <= " << *filter.energyMax;

    if (filter.ratingMin) sql << " AND rating >= " << *filter.ratingMin;
    if (filter.ratingMax) sql << " AND rating <= " << *filter.ratingMax;

    if (filter.yearMin) sql << " AND year >= " << *filter.yearMin;
    if (filter.yearMax) sql << " AND year <= " << *filter.yearMax;

    if (filter.mood) { sql << " AND mood LIKE ?"; params.push_back("%" + *filter.mood + "%"); }
    if (!filter.moods.empty()) {
        sql << " AND (";
        bool first = true;
        for (const auto& m : filter.moods) {
            if (!first) sql << " OR ";
            sql << "mood LIKE ?";
            params.push_back("%" + m + "%");
            first = false;
        }
        sql << ")";
    }

    if (filter.danceabilityMin) sql << " AND danceability >= " << *filter.danceabilityMin;
    if (filter.danceabilityMax) sql << " AND danceability <= " << *filter.danceabilityMax;

    if (filter.durationMin) sql << " AND duration >= " << *filter.durationMin;
    if (filter.durationMax) sql << " AND duration <= " << *filter.durationMax;

    if (filter.fileFormat) { sql << " AND file_format = ?"; params.push_back(*filter.fileFormat); }
    if (!filter.fileFormats.empty()) {
        sql << " AND file_format IN (";
        bool first = true;
        for (const auto& f : filter.fileFormats) {
            if (!first) sql << ", ";
            sql << "?";
            params.push_back(f);
            first = false;
        }
        sql << ")";
    }

    if (filter.bitRateMin) sql << " AND bit_rate >= " << *filter.bitRateMin;

    if (filter.lossless) {
        if (*filter.lossless)
            sql << " AND file_format IN ('flac', 'wav', 'aiff', 'aif', 'alac')";
        else
            sql << " AND file_format NOT IN ('flac', 'wav', 'aiff', 'aif', 'alac')";
    }

    if (filter.analyzed) sql << " AND analyzed = " << (*filter.analyzed ? 1 : 0);

    if (filter.source) sql << " AND source = " << static_cast<int>(*filter.source);

    if (filter.playCountMin) sql << " AND play_count >= " << *filter.playCountMin;
    if (filter.playCountMax) sql << " AND play_count <= " << *filter.playCountMax;
    if (filter.neverPlayed && *filter.neverPlayed) sql << " AND play_count = 0";

    if (filter.lastPlayedAfter) sql << " AND last_played >= " << *filter.lastPlayedAfter;
    if (filter.lastPlayedBefore) sql << " AND last_played <= " << *filter.lastPlayedBefore;

    if (filter.addedAfter) sql << " AND date_added >= " << *filter.addedAfter;
    if (filter.addedBefore) sql << " AND date_added <= " << *filter.addedBefore;

    if (filter.color) { sql << " AND color = ?"; params.push_back(*filter.color); }
    if (filter.label) { sql << " AND label LIKE ?"; params.push_back("%" + *filter.label + "%"); }

    if (filter.sortBy != "relevance") {
        std::string col = filter.sortBy;
        if (col == "date_added" || col == "bpm" || col == "rating" || col == "energy"
            || col == "title" || col == "artist" || col == "duration" || col == "year"
            || col == "play_count" || col == "last_played" || col == "genre") {
            sql << " ORDER BY " << col << (filter.ascending ? " ASC" : " DESC");
        }
    }

    sql << " LIMIT " << filter.limit;
    if (filter.offset > 0) sql << " OFFSET " << filter.offset;

    return sql.str();
}

float AdvancedSearchService::calculateRelevanceScore(const Models::Track& track, const AdvancedFilter& filter) const {
    float score = 1.0f;

    if (!filter.query.empty()) {
        std::string q = filter.query;
        std::transform(q.begin(), q.end(), q.begin(), ::tolower);

        auto containsLower = [&q](const std::string& s) {
            std::string lower = s;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            return lower.find(q) != std::string::npos;
        };

        if (containsLower(track.title)) score += 3.0f;
        if (containsLower(track.artist)) score += 2.0f;
        if (containsLower(track.album)) score += 1.0f;
    }

    if (track.rating > 0) score += track.rating * 0.2f;

    if (track.analyzed) score += 0.5f;

    return score;
}

std::vector<std::string> AdvancedSearchService::determineMatchedFields(const Models::Track& track,
                                                                         const AdvancedFilter& filter) const {
    std::vector<std::string> matched;

    if (!filter.query.empty()) {
        std::string q = filter.query;
        std::transform(q.begin(), q.end(), q.begin(), ::tolower);

        auto check = [&q](const std::string& field, const std::string& name, std::vector<std::string>& out) {
            std::string lower = field;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find(q) != std::string::npos) out.push_back(name);
        };

        check(track.title, "title", matched);
        check(track.artist, "artist", matched);
        check(track.album, "album", matched);
        check(track.genre, "genre", matched);
    }

    if (filter.bpmMin || filter.bpmMax) matched.push_back("bpm");
    if (filter.key) matched.push_back("key");
    if (filter.energyMin || filter.energyMax) matched.push_back("energy");
    if (filter.ratingMin) matched.push_back("rating");

    return matched;
}

} // namespace BeatMate::Services::Library
