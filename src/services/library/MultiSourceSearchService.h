#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <future>
#include <mutex>
#include <atomic>
#include <map>
#include <cstdint>

#include <spdlog/spdlog.h>

#include "../../models/Track.h"

namespace BeatMate::Services::Library {

class TrackDatabase;
class AdvancedSearchService;

enum class SearchSource {
    Local,
    VirtualDJ,
    Rekordbox,
    Serato,
    Traktor,
    EngineDJ,
    ITunes,
    Streaming
};

struct SearchSourceConfig {
    SearchSource source;
    std::string name;
    std::string databasePath;
    bool enabled = true;
    int priority = 0;       // Higher = searched first
    int timeoutMs = 5000;
};

struct MultiSourceResult {
    Models::Track track;
    SearchSource source;
    std::string sourceName;
    float relevance = 0.0f;
    bool isLocal = false;
    std::string externalId;
};

struct MultiSourceSearchResults {
    std::vector<MultiSourceResult> results;
    std::map<SearchSource, int> resultCountBySource;
    double searchDurationMs = 0.0;
    std::vector<std::string> errors;
    int totalResults() const {
        int total = 0;
        for (const auto& [_, count] : resultCountBySource) total += count;
        return total;
    }
};

using MultiSourceProgressCallback = std::function<void(SearchSource source, int resultsFound)>;

class MultiSourceSearchService {
public:
    explicit MultiSourceSearchService(std::shared_ptr<TrackDatabase> database);
    ~MultiSourceSearchService() = default;

    void addSource(const SearchSourceConfig& config);
    void removeSource(SearchSource source);
    void enableSource(SearchSource source, bool enabled);
    std::vector<SearchSourceConfig> getSources() const;

    MultiSourceSearchResults searchAll(const std::string& query, int limit = 100,
                                        MultiSourceProgressCallback progressCb = nullptr);

    std::vector<MultiSourceResult> searchSource(SearchSource source, const std::string& query, int limit = 50);

    std::vector<SearchSourceConfig> detectInstalledSources();

    int64_t importFromSource(const MultiSourceResult& result);
    std::vector<int64_t> importFromSource(const std::vector<MultiSourceResult>& results);

    bool isSourceAvailable(SearchSource source) const;
    std::string getSourceDatabasePath(SearchSource source) const;

private:
    std::vector<MultiSourceResult> searchLocal(const std::string& query, int limit);
    std::vector<MultiSourceResult> searchVirtualDJ(const std::string& query, int limit);
    std::vector<MultiSourceResult> searchRekordbox(const std::string& query, int limit);
    std::vector<MultiSourceResult> searchSerato(const std::string& query, int limit);
    std::vector<MultiSourceResult> searchTraktor(const std::string& query, int limit);
    std::vector<MultiSourceResult> searchEngineDJ(const std::string& query, int limit);
    std::vector<MultiSourceResult> searchITunes(const std::string& query, int limit);

    std::string detectVirtualDJPath() const;
    std::string detectRekordboxPath() const;
    std::string detectSeratoPath() const;
    std::string detectTraktorPath() const;
    std::string detectEngineDJPath() const;
    std::string detectITunesPath() const;

    void deduplicateResults(std::vector<MultiSourceResult>& results) const;

    std::shared_ptr<TrackDatabase> database_;
    std::vector<SearchSourceConfig> sources_;
    mutable std::mutex mutex_;
};

} // namespace BeatMate::Services::Library
