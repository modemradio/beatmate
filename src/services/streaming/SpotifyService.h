#pragma once
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "StreamingServiceBase.h"
#include "../../models/SpotifyModels.h"

namespace BeatMate::Services::Streaming {

enum class SpotifySearchType {
    Track,
    Album,
    Artist,
    Playlist,
    Show,
    Episode
};

struct SpotifyRecommendationParams {
    std::vector<std::string> seedArtists;
    std::vector<std::string> seedTracks;
    std::vector<std::string> seedGenres;
    float minEnergy = -1.0f;
    float maxEnergy = -1.0f;
    float targetEnergy = -1.0f;
    float minDanceability = -1.0f;
    float maxDanceability = -1.0f;
    float targetDanceability = -1.0f;
    float minTempo = -1.0f;
    float maxTempo = -1.0f;
    float targetTempo = -1.0f;
    float minValence = -1.0f;
    float maxValence = -1.0f;
    int limit = 20;
};

struct SpotifyUserProfile {
    std::string id;
    std::string displayName;
    std::string email;
    std::string country;
    std::string product;
    int followers = 0;
    std::string profileImageUrl;
};

class SpotifyService : public StreamingServiceBase {
public:
    SpotifyService();
    ~SpotifyService() override = default;

    bool authenticate(const std::string& clientId, const std::string& redirectUri,
                      const std::vector<std::string>& scopes) override;
    bool refreshAccessToken() override;
    std::string getAuthorizationUrl(const std::string& clientId, const std::string& redirectUri,
                                     const std::vector<std::string>& scopes);
    bool exchangeCode(const std::string& code, const std::string& codeVerifier);
    void setStateVerifier(const std::string& state) { stateVerifier_ = state; }
    bool validateState(const std::string& receivedState) const;

    StreamingSearchResult search(const std::string& query, int limit = 20, int offset = 0) override;
    StreamingSearchResult searchWithFilters(const std::string& query,
                                            const std::vector<SpotifySearchType>& types,
                                            const std::string& market = "",
                                            int limit = 20, int offset = 0);

    std::optional<Models::StreamingTrack> getTrack(const std::string& trackId) override;
    Models::SpotifyTrack getSpotifyTrack(const std::string& trackId);
    std::vector<Models::SpotifyTrack> getMultipleTracks(const std::vector<std::string>& trackIds);
    bool isTrackSaved(const std::string& trackId);
    bool saveTrack(const std::string& trackId);
    bool unsaveTrack(const std::string& trackId);

    Models::SpotifyAudioFeatures getAudioFeatures(const std::string& trackId);
    std::vector<Models::SpotifyAudioFeatures> getMultipleAudioFeatures(const std::vector<std::string>& trackIds);

    std::vector<Models::Playlist> getPlaylists() override;
    std::vector<Models::SpotifyPlaylist> getSpotifyPlaylists(int limit = 50, int offset = 0);
    Models::SpotifyPlaylist getPlaylistDetails(const std::string& playlistId);
    std::vector<Models::SpotifyTrack> getPlaylistTracks(const std::string& playlistId, int limit = 100, int offset = 0);
    std::string createPlaylist(const std::string& name, const std::string& description = "",
                                bool isPublic = false);
    bool addTracksToPlaylist(const std::string& playlistId, const std::vector<std::string>& trackUris);
    bool removeTracksFromPlaylist(const std::string& playlistId, const std::vector<std::string>& trackUris);
    bool reorderPlaylistTracks(const std::string& playlistId, int rangeStart, int insertBefore, int rangeLength = 1);

    Models::SpotifyAlbum getAlbum(const std::string& albumId);
    std::vector<Models::SpotifyTrack> getAlbumTracks(const std::string& albumId);
    std::vector<Models::SpotifyAlbum> getNewReleases(const std::string& country = "US", int limit = 20);

    Models::SpotifyArtist getArtist(const std::string& artistId);
    std::vector<Models::SpotifyTrack> getArtistTopTracks(const std::string& artistId, const std::string& market = "US");
    std::vector<Models::SpotifyArtist> getRelatedArtists(const std::string& artistId);
    std::vector<Models::SpotifyAlbum> getArtistAlbums(const std::string& artistId, int limit = 20);

    std::vector<Models::SpotifyTrack> getSavedTracks(int limit = 50, int offset = 0);
    std::vector<Models::SpotifyAlbum> getSavedAlbums(int limit = 50, int offset = 0);
    std::vector<Models::SpotifyTrack> getRecentlyPlayed(int limit = 50);
    std::vector<Models::SpotifyTrack> getTopTracks(const std::string& timeRange = "medium_term", int limit = 50);
    std::vector<Models::SpotifyArtist> getTopArtists(const std::string& timeRange = "medium_term", int limit = 50);

    std::vector<Models::SpotifyTrack> getRecommendations(const SpotifyRecommendationParams& params);
    std::vector<std::string> getAvailableGenreSeeds();

    SpotifyUserProfile getCurrentUserProfile();
    SpotifyUserProfile getUserProfile(const std::string& userId);

    bool startPlayback(const std::string& contextUri = "", const std::vector<std::string>& uris = {},
                        int offsetPosition = 0);
    bool pausePlayback();
    bool skipToNext();
    bool skipToPrevious();
    bool seekToPosition(int positionMs);
    bool setVolume(int volumePercent);
    bool setRepeatMode(const std::string& state);
    bool toggleShuffle(bool state);
    bool addToQueue(const std::string& uri);

    static std::string generateCodeVerifier();
    static std::string generateCodeChallenge(const std::string& verifier);
    static std::string generateRandomState(int length = 32);

private:
    std::string makeApiRequest(const std::string& endpoint);
    std::string makeApiPostRequest(const std::string& endpoint, const std::string& body);
    std::string makeApiPutRequest(const std::string& endpoint, const std::string& body = "");
    std::string makeApiDeleteRequest(const std::string& endpoint, const std::string& body = "");

    std::string clientId_;
    std::string redirectUri_;
    std::string codeVerifier_;
    std::string stateVerifier_;

    std::string tokenCachePath_;
    void saveTokenToCache();
    bool loadTokenFromCache();

    static constexpr const char* API_BASE = "https://api.spotify.com/v1";
    static constexpr const char* AUTH_URL = "https://accounts.spotify.com/authorize";
    static constexpr const char* TOKEN_URL = "https://accounts.spotify.com/api/token";
};

}
