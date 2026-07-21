#pragma once

#include <string>
#include <vector>
#include <functional>
#include <map>
#include <mutex>
#include <cstdint>
#include <utility>

#include "../../models/Track.h"
#include "../../models/CuePoint.h"
#include "../../models/Playlist.h"
#include "../../models/TrackAnalysis.h"
#include "../../models/SmartPlaylistRule.h"

namespace BeatMate::Services::Library {

class TrackDatabase;

// Thread-safe (verrouillage interne) ; les vues s'abonnent via onDataChanged().
class TrackDataProvider {
public:
    explicit TrackDataProvider(TrackDatabase& db);
    ~TrackDataProvider() = default;

    TrackDataProvider(const TrackDataProvider&) = delete;
    TrackDataProvider& operator=(const TrackDataProvider&) = delete;

    std::vector<Models::Track> getAllTracks();
    std::vector<Models::Track> searchTracks(const std::string& query);
    std::vector<Models::Track> getTracksByFilter(
        float bpmMin, float bpmMax,
        const std::string& key,
        const std::string& genre,
        float energyMin, float energyMax,
        int ratingMin);
    std::vector<Models::Track> getTracksByFilterWithSearch(
        const std::string& searchText,
        float bpmMin, float bpmMax,
        const std::string& key,
        const std::string& genre,
        const std::string& artist = "",
        float energyMin = 1, float energyMax = 10,
        int ratingMin = 0);
    std::vector<Models::Track> getTracksBySource(Models::TrackSource source);
    Models::Track getTrack(int64_t id);

    std::vector<std::string> getTrackTags(int64_t trackId);
    bool setTrackTags(int64_t trackId, const std::vector<std::string>& tags);
    std::vector<std::string> getAllTags();
    int64_t addTrack(const Models::Track& track);
    void updateTrack(const Models::Track& track);
    void deleteTrack(int64_t id);

    int getTotalTracks();
    int getAnalyzedTracks();
    std::vector<std::pair<std::string, int>> getGenreDistribution();
    std::vector<std::pair<std::string, int>> getArtistDistribution();
    std::vector<Models::Track> getRecentlyAdded(int limit = 5);
    std::vector<Models::Track> getRecentlyPlayed(int limit = 5);
    std::vector<Models::Track> getMostPlayed(int limit = 10);

    std::vector<Models::Track> getSuggestionsFor(const Models::Track& current, int limit = 10);
    std::vector<Models::Track> getCompatibleTracks(float bpm, const std::string& key, int limit = 20);

    std::vector<Models::Playlist> getAllPlaylists();
    std::vector<Models::Track> getPlaylistTracks(int64_t playlistId);
    int64_t createPlaylist(const std::string& name);
    bool renamePlaylist(int64_t playlistId, const std::string& name);
    bool deletePlaylist(int64_t playlistId);
    void addToPlaylist(int64_t playlistId, int64_t trackId);
    void removeFromPlaylist(int64_t playlistId, int64_t trackId);
    bool reorderPlaylist(int64_t playlistId, const std::vector<int64_t>& trackIds);
    bool setPlaylistTracks(int64_t playlistId, const std::vector<int64_t>& trackIds);

    int64_t createSmartPlaylist(const std::string& name,
                                const Models::SmartPlaylistRuleGroup& rules);
    bool updateSmartPlaylistRules(int64_t playlistId,
                                  const Models::SmartPlaylistRuleGroup& rules);
    Models::SmartPlaylistRuleGroup getSmartPlaylistRules(int64_t playlistId);
    std::vector<Models::Track> refreshSmartPlaylist(int64_t playlistId);
    int countSmartMatches(const Models::SmartPlaylistRuleGroup& rules);

    std::vector<Models::Track> suggestForPlaylist(const std::vector<int64_t>& playlistTrackIds,
                                                  int maxResults = 20);
    std::vector<Models::Track> suggestFillToDuration(const std::vector<int64_t>& playlistTrackIds,
                                                     double targetMinutes);

    bool exportPlaylistToFile(const std::vector<Models::Track>& tracks,
                              const std::string& name,
                              const std::string& outputPath,
                              const std::string& format);

    bool sendTracksToDJ(const std::vector<Models::Track>& tracks, std::string& outMessage);

    std::vector<Models::Track> getUnanalyzedTracks();
    void saveAnalysis(int64_t trackId, const Models::TrackAnalysis& analysis);

    std::vector<Models::CuePoint> getCuePoints(int64_t trackId);
    std::map<int64_t, int> getCueCounts();
    int64_t saveCuePoint(const Models::CuePoint& cue);
    void deleteCuePoint(int64_t cueId);

    void recordPlay(int64_t trackId);
    void recordPlayByPath(const std::string& filePath);

    std::vector<Models::Track> getTracksForSet(const std::string& genre, float bpmTarget, int limit = 50);

    using DataChangedCallback = std::function<void()>;
    void onDataChanged(DataChangedCallback cb);

    // Lightweight notification for cue/minor changes (no full DB reload)
    using LightChangedCallback = std::function<void()>;
    void onLightChanged(LightChangedCallback cb);

    // Batch mode: suppress notifications during bulk operations
    void beginBatch();
    void endBatch(); // fires one notifyDataChanged at the end

    int64_t getTrackCount();

    // Force la relecture SQL au prochain getAllTracks (apres une ecriture externe).
    void invalidateAllTracksCache();

private:
    void notifyDataChanged();
    void notifyLightChanged();

    TrackDatabase& db_;
    std::vector<DataChangedCallback> callbacks_;
    std::vector<LightChangedCallback> lightCallbacks_;
    std::mutex callbackMutex_;
    std::atomic<bool> batchMode_{false};

    // Cache de getAllTracks() partage entre les vues au boot.
    std::mutex allTracksCacheMutex_;
    std::vector<Models::Track> allTracksCache_;
    bool allTracksCacheValid_ = false;
};

} // namespace BeatMate::Services::Library
