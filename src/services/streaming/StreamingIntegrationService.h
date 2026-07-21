#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "StreamingServiceBase.h"
#include "StreamingAccountService.h"
#include "../../models/Playlist.h"

namespace BeatMate::Services::Streaming {

struct UnifiedTrackResult {
    Models::StreamingTrack track;
    Models::StreamingServiceType source;
    double relevanceScore = 0.0;
    bool availableOnMultiple = false;
    std::vector<Models::StreamingServiceType> availableSources;
};

struct UnifiedPlaylist {
    std::string name;
    std::string description;
    Models::StreamingServiceType primarySource;
    int totalTracks = 0;
    std::vector<UnifiedTrackResult> tracks;
};

struct CrossPlatformSearchOptions {
    std::string query;
    int limitPerService = 10;
    bool searchAllConnected = true;
    std::vector<Models::StreamingServiceType> specificServices;
    bool deduplicateByISRC = true;
    bool mergeResults = true;
};

class StreamingIntegrationService {
public:
    StreamingIntegrationService();
    ~StreamingIntegrationService() = default;

    void setAccountService(std::shared_ptr<StreamingAccountService> accountService);

    std::vector<UnifiedTrackResult> searchAllPlatforms(const std::string& query, int limitPerService = 10);
    std::vector<UnifiedTrackResult> searchWithOptions(const CrossPlatformSearchOptions& options);
    std::optional<UnifiedTrackResult> findTrackByISRC(const std::string& isrc);
    std::vector<UnifiedTrackResult> findTrackOnOtherPlatforms(const Models::StreamingTrack& track);

    UnifiedTrackResult resolveTrack(const std::string& title, const std::string& artist);
    std::vector<UnifiedTrackResult> resolveMultipleTracks(
        const std::vector<std::pair<std::string, std::string>>& titleArtistPairs);

    bool syncPlaylistToPlatform(const Models::Playlist& playlist,
                                 Models::StreamingServiceType targetPlatform,
                                 const std::vector<Models::StreamingTrack>& tracks);
    bool syncPlaylistFromPlatform(Models::StreamingServiceType source,
                                   const std::string& playlistId,
                                   Models::Playlist& outPlaylist,
                                   std::vector<Models::StreamingTrack>& outTracks);
    bool crossSyncPlaylist(const std::string& sourcePlaylistId,
                            Models::StreamingServiceType sourcePlatform,
                            Models::StreamingServiceType targetPlatform);

    std::vector<UnifiedTrackResult> getUnifiedRecentlyPlayed(int limit = 50);
    std::vector<UnifiedPlaylist> getUnifiedPlaylists();
    std::vector<UnifiedTrackResult> getUnifiedSavedTracks(int limitPerService = 50);

    struct PlatformAvailability {
        std::string title;
        std::string artist;
        std::map<Models::StreamingServiceType, bool> available;
        std::map<Models::StreamingServiceType, std::string> serviceIds;
    };
    std::vector<PlatformAvailability> checkAvailability(
        const std::vector<std::pair<std::string, std::string>>& titleArtistPairs);

    struct IntegrationStats {
        int totalConnectedServices = 0;
        int totalUniqueTracks = 0;
        int totalPlaylists = 0;
        std::map<Models::StreamingServiceType, int> tracksPerService;
        std::map<Models::StreamingServiceType, int> playlistsPerService;
    };
    IntegrationStats getStats() const;

private:
    std::vector<UnifiedTrackResult> deduplicateByISRC(std::vector<UnifiedTrackResult>& tracks) const;
    std::vector<UnifiedTrackResult> mergeAndRank(std::vector<UnifiedTrackResult>& tracks) const;
    double computeRelevanceScore(const Models::StreamingTrack& track, const std::string& query) const;

    std::shared_ptr<StreamingAccountService> accountService_;
};

} // namespace BeatMate::Services::Streaming
