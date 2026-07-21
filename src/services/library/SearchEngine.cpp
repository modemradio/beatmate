#include "SearchEngine.h"
#include "TrackDatabase.h"

#include <spdlog/spdlog.h>
#include <sstream>
#include <algorithm>

namespace BeatMate::Services::Library {

SearchEngine::SearchEngine(std::shared_ptr<TrackDatabase> database)
    : database_(std::move(database)) {
}

std::vector<SearchResult> SearchEngine::search(const std::string& query, int limit) {
    std::vector<SearchResult> results;

    if (query.empty()) return results;
    if (!database_) {
        spdlog::debug("SearchEngine: No database, skipping search");
        return results;
    }

    auto tracks = database_->searchFTS(query, limit);

    float maxRank = static_cast<float>(tracks.size());
    for (size_t i = 0; i < tracks.size(); ++i) {
        SearchResult result;
        result.track = tracks[i];
        result.relevance = (maxRank - static_cast<float>(i)) / maxRank;
        result.matchedField = "fts";
        results.push_back(result);
    }

    spdlog::debug("SearchEngine: '{}' returned {} results", query, results.size());
    return results;
}

std::vector<SearchResult> SearchEngine::advancedSearch(const SearchCriteria& criteria) {
    if (!database_) {
        spdlog::debug("SearchEngine: No database, skipping advancedSearch");
        return {};
    }
    std::vector<std::string> params;
    std::string sql = buildAdvancedQuery(criteria, params);
    spdlog::debug("SearchEngine: Advanced query: {}", sql);

    auto tracks = database_->getTracksByQuery(sql, params);

    std::vector<SearchResult> results;
    for (auto& track : tracks) {
        SearchResult result;
        result.track = std::move(track);
        result.relevance = 1.0f;
        result.matchedField = "advanced";
        results.push_back(std::move(result));
    }

    spdlog::debug("SearchEngine: Advanced search returned {} results", results.size());
    return results;
}

bool SearchEngine::rebuildIndex() {
    if (!database_) {
        spdlog::debug("SearchEngine: No database, skipping rebuildIndex");
        return false;
    }
    spdlog::info("SearchEngine: Rebuilding FTS index");
    return database_->rebuildFTSIndex();
}

std::vector<std::string> SearchEngine::getSuggestions(const std::string& prefix, int limit) {
    std::vector<std::string> suggestions;
    if (prefix.empty()) return suggestions;
    if (!database_) {
        spdlog::debug("SearchEngine: No database, skipping getSuggestions");
        return suggestions;
    }

    auto tracks = database_->getTracksByQuery(
        "SELECT DISTINCT title FROM tracks WHERE title LIKE ? ORDER BY title LIMIT ?",
        {prefix + "%", std::to_string(limit)}
    );

    for (const auto& track : tracks) {
        if (!track.title.empty()) suggestions.push_back(track.title);
    }

    auto artistTracks = database_->getTracksByQuery(
        "SELECT DISTINCT artist FROM tracks WHERE artist LIKE ? ORDER BY artist LIMIT ?",
        {prefix + "%", std::to_string(limit)}
    );

    for (const auto& track : artistTracks) {
        if (!track.artist.empty()) suggestions.push_back(track.artist);
    }

    std::sort(suggestions.begin(), suggestions.end());
    suggestions.erase(std::unique(suggestions.begin(), suggestions.end()), suggestions.end());
    if (static_cast<int>(suggestions.size()) > limit) {
        suggestions.resize(static_cast<size_t>(limit));
    }

    return suggestions;
}

std::string SearchEngine::buildAdvancedQuery(const SearchCriteria& criteria,
                                             std::vector<std::string>& params) const {
    std::ostringstream sql;
    sql << "SELECT * FROM tracks WHERE 1=1";

    if (!criteria.query.empty()) {
        sql << " AND (title LIKE ? OR artist LIKE ? OR album LIKE ?)";
        const std::string like = "%" + criteria.query + "%";
        params.push_back(like);
        params.push_back(like);
        params.push_back(like);
    }

    if (criteria.artist) {
        sql << " AND artist LIKE ?";
        params.push_back("%" + *criteria.artist + "%");
    }

    if (criteria.genre) {
        sql << " AND genre LIKE ?";
        params.push_back("%" + *criteria.genre + "%");
    }

    if (criteria.album) {
        sql << " AND album LIKE ?";
        params.push_back("%" + *criteria.album + "%");
    }

    if (criteria.bpmMin) {
        sql << " AND bpm >= " << *criteria.bpmMin;
    }
    if (criteria.bpmMax) {
        sql << " AND bpm <= " << *criteria.bpmMax;
    }

    if (criteria.key) {
        sql << " AND (key = ? OR camelot_key = ?)";
        params.push_back(*criteria.key);
        params.push_back(*criteria.key);
    }

    if (criteria.ratingMin) {
        sql << " AND rating >= " << *criteria.ratingMin;
    }

    if (criteria.energyMin) {
        sql << " AND energy >= " << *criteria.energyMin;
    }
    if (criteria.energyMax) {
        sql << " AND energy <= " << *criteria.energyMax;
    }

    if (criteria.yearMin) {
        sql << " AND year >= " << *criteria.yearMin;
    }
    if (criteria.yearMax) {
        sql << " AND year <= " << *criteria.yearMax;
    }

    if (criteria.sortBy == "relevance") {
    } else if (criteria.sortBy == "title") {
        sql << " ORDER BY title " << (criteria.ascending ? "ASC" : "DESC");
    } else if (criteria.sortBy == "artist") {
        sql << " ORDER BY artist " << (criteria.ascending ? "ASC" : "DESC");
    } else if (criteria.sortBy == "bpm") {
        sql << " ORDER BY bpm " << (criteria.ascending ? "ASC" : "DESC");
    } else if (criteria.sortBy == "rating") {
        sql << " ORDER BY rating " << (criteria.ascending ? "ASC" : "DESC");
    } else if (criteria.sortBy == "date_added") {
        sql << " ORDER BY date_added " << (criteria.ascending ? "ASC" : "DESC");
    }

    sql << " LIMIT " << criteria.limit;

    return sql.str();
}

} // namespace BeatMate::Services::Library
