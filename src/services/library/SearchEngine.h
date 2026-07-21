#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>

#include "../../models/Track.h"

namespace BeatMate::Services::Library {

class TrackDatabase;

struct SearchResult {
    Models::Track track;
    float relevance = 0.0f;
    std::string matchedField;
};

struct SearchCriteria {
    std::string query;
    std::optional<std::string> artist;
    std::optional<std::string> genre;
    std::optional<std::string> album;
    std::optional<double> bpmMin;
    std::optional<double> bpmMax;
    std::optional<std::string> key;
    std::optional<int> ratingMin;
    std::optional<float> energyMin;
    std::optional<float> energyMax;
    std::optional<int> yearMin;
    std::optional<int> yearMax;
    int limit = 100;
    std::string sortBy = "relevance";
    bool ascending = false;
};

class SearchEngine {
public:
    explicit SearchEngine(std::shared_ptr<TrackDatabase> database);
    ~SearchEngine() = default;

    std::vector<SearchResult> search(const std::string& query, int limit = 100);
    std::vector<SearchResult> advancedSearch(const SearchCriteria& criteria);
    bool rebuildIndex();
    std::vector<std::string> getSuggestions(const std::string& prefix, int limit = 10);

private:
    std::string buildAdvancedQuery(const SearchCriteria& criteria,
                                   std::vector<std::string>& params) const;
    std::shared_ptr<TrackDatabase> database_;
};

} // namespace BeatMate::Services::Library
