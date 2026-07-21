#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <set>
#include <functional>
#include <cstdint>

#include <spdlog/spdlog.h>

#include "../../models/Track.h"

namespace BeatMate::Services::Library {

class TrackDatabase;

struct AdvancedFilter {
    std::string query;
    std::optional<std::string> titleFilter;
    std::optional<std::string> artistFilter;
    std::optional<std::string> albumFilter;

    std::optional<double> bpmMin;
    std::optional<double> bpmMax;
    double bpmTolerance = 0.0;

    std::optional<std::string> key;
    bool includeCompatibleKeys = false;

    std::optional<std::string> genre;
    std::set<std::string> genres; // Multiple genres (OR)

    std::optional<float> energyMin;
    std::optional<float> energyMax;

    std::optional<int> ratingMin;
    std::optional<int> ratingMax;

    std::optional<int> yearMin;
    std::optional<int> yearMax;

    std::optional<std::string> mood;
    std::set<std::string> moods; // Multiple moods (OR)

    std::optional<float> danceabilityMin;
    std::optional<float> danceabilityMax;

    std::optional<double> durationMin;
    std::optional<double> durationMax;

    std::optional<std::string> fileFormat;
    std::set<std::string> fileFormats;
    std::optional<int> bitRateMin;
    std::optional<bool> lossless;

    std::optional<bool> analyzed;

    std::optional<Models::TrackSource> source;

    std::optional<int> playCountMin;
    std::optional<int> playCountMax;
    std::optional<int64_t> lastPlayedAfter;
    std::optional<int64_t> lastPlayedBefore;
    std::optional<bool> neverPlayed;

    std::optional<int64_t> addedAfter;
    std::optional<int64_t> addedBefore;

    std::optional<std::string> color;
    std::optional<std::string> label;

    std::string sortBy = "relevance";
    bool ascending = false;

    int limit = 200;
    int offset = 0;
};

struct AdvancedSearchResult {
    Models::Track track;
    float score = 0.0f;
    std::vector<std::string> matchedFields;
};

struct SearchPreset {
    std::string name;
    std::string description;
    AdvancedFilter filter;
    int64_t createdAt = 0;
};

class AdvancedSearchService {
public:
    explicit AdvancedSearchService(std::shared_ptr<TrackDatabase> database);
    ~AdvancedSearchService() = default;

    std::vector<AdvancedSearchResult> search(const AdvancedFilter& filter);

    std::vector<AdvancedSearchResult> searchByBPMRange(double minBPM, double maxBPM, int limit = 100);
    std::vector<AdvancedSearchResult> searchByKey(const std::string& key, bool includeCompatible = false, int limit = 100);
    std::vector<AdvancedSearchResult> searchByGenre(const std::string& genre, int limit = 100);
    std::vector<AdvancedSearchResult> searchByEnergyRange(float minEnergy, float maxEnergy, int limit = 100);
    std::vector<AdvancedSearchResult> searchByMood(const std::string& mood, int limit = 100);
    std::vector<AdvancedSearchResult> searchRecentlyAdded(int days = 30, int limit = 100);
    std::vector<AdvancedSearchResult> searchNeverPlayed(int limit = 100);
    std::vector<AdvancedSearchResult> searchMostPlayed(int limit = 100);
    std::vector<AdvancedSearchResult> searchHighRated(int minRating = 4, int limit = 100);
    std::vector<AdvancedSearchResult> searchUnanalyzed(int limit = 100);

    static std::vector<std::string> getCompatibleKeys(const std::string& key);
    static bool areKeysCompatible(const std::string& key1, const std::string& key2);

    void savePreset(const SearchPreset& preset);
    std::vector<SearchPreset> getPresets() const;
    void deletePreset(const std::string& name);

    std::vector<std::string> getRecentSearches(int limit = 10) const;
    void addRecentSearch(const std::string& query);
    void clearRecentSearches();

private:
    std::string buildSQL(const AdvancedFilter& filter,
                         std::vector<std::string>& params) const;
    float calculateRelevanceScore(const Models::Track& track, const AdvancedFilter& filter) const;
    std::vector<std::string> determineMatchedFields(const Models::Track& track, const AdvancedFilter& filter) const;

    std::shared_ptr<TrackDatabase> database_;
    std::vector<SearchPreset> presets_;
    std::vector<std::string> recentSearches_;
    mutable std::mutex mutex_;
};

} // namespace BeatMate::Services::Library
