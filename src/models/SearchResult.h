#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

struct SearchResult {
    int64_t trackId = 0;
    float score = 0.0f;
    std::vector<std::string> matchedFields;

    std::string highlightedTitle;
    std::string highlightedArtist;
    std::string highlightedAlbum;

    std::string title;
    std::string artist;
    std::string album;
    double bpm = 0.0;
    std::string key;
    float energy = 0.0f;
    double duration = 0.0;
    int rating = 0;

    SearchResult() = default;

    SearchResult(int64_t trackId, float score)
        : trackId(trackId), score(score) {}

    bool operator==(const SearchResult& other) const { return trackId == other.trackId; }

    bool operator<(const SearchResult& other) const {
        return score > other.score;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SearchResult,
        trackId, score, matchedFields,
        highlightedTitle, highlightedArtist, highlightedAlbum,
        title, artist, album, bpm, key, energy, duration, rating
    )
};

struct SearchQuery {
    std::string text;
    std::vector<std::string> fields;
    int maxResults = 50;
    int offset = 0;
    float minScore = 0.0f;
    bool fuzzyMatch = true;
    bool wholeWord = false;

    double bpmMin = 0.0;
    double bpmMax = 0.0;
    std::string keyFilter;
    std::string genreFilter;
    int ratingMin = 0;
    float energyMin = 0.0f;
    float energyMax = 0.0f;

    SearchQuery() = default;

    explicit SearchQuery(const std::string& text) : text(text) {}

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SearchQuery,
        text, fields, maxResults, offset, minScore, fuzzyMatch, wholeWord,
        bpmMin, bpmMax, keyFilter, genreFilter, ratingMin, energyMin, energyMax
    )
};

struct SearchResults {
    std::vector<SearchResult> results;
    int totalCount = 0;
    double searchTimeMs = 0.0;
    std::string query;

    SearchResults() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SearchResults,
        results, totalCount, searchTimeMs, query
    )
};

}
