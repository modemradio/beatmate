#pragma once
#include <optional>
#include <string>
#include <vector>
#include "StreamingServiceBase.h"

namespace BeatMate::Services::Streaming {

enum class TidalQuality {
    Low,
    High,
    Lossless,
    HiRes,
    Master
};

struct TidalTrack {
    int64_t id = 0;
    std::string title;
    std::string artistName;
    std::string albumName;
    std::string isrc;
    int durationSec = 0;
    int trackNumber = 0;
    int volumeNumber = 1;
    std::string quality;
    std::string artworkUrl;
    std::string url;
    int popularity = 0;
    bool isExplicit = false;
    bool allowStreaming = true;
    int64_t albumId = 0;
    std::string copyright;
    float replayGain = 0.0f;
    float peakAmplitude = 1.0f;
};

struct TidalAlbum {
    int64_t id = 0;
    std::string title;
    std::string artistName;
    std::string artworkUrl;
    std::string releaseDate;
    int numTracks = 0;
    int numVolumes = 1;
    std::string quality;
    int durationSec = 0;
    std::string copyright;
    bool isExplicit = false;
    std::string type;
};

struct TidalPlaylist {
    std::string uuid;
    std::string title;
    std::string description;
    std::string artworkUrl;
    int numTracks = 0;
    int durationSec = 0;
    std::string creatorName;
    std::string type;  // "USER", "EDITORIAL"
    std::string created;
    std::string lastUpdated;
    std::vector<TidalTrack> tracks;
};

struct TidalArtist {
    int64_t id = 0;
    std::string name;
    std::string artworkUrl;
    int popularity = 0;
};

class TidalService : public StreamingServiceBase {
public:
    TidalService();
    ~TidalService() override = default;

    bool authenticate(const std::string& clientId, const std::string& redirectUri,
                      const std::vector<std::string>& scopes) override;
    bool refreshAccessToken() override;
    bool exchangeCode(const std::string& code, const std::string& codeVerifier);
    std::string getAuthorizationUrl(const std::string& clientId, const std::string& redirectUri);

    StreamingSearchResult search(const std::string& query, int limit = 20, int offset = 0) override;
    std::vector<TidalTrack> searchTracks(const std::string& query, int limit = 50, int offset = 0);
    std::vector<TidalAlbum> searchAlbums(const std::string& query, int limit = 20);
    std::vector<TidalArtist> searchArtists(const std::string& query, int limit = 20);
    std::vector<TidalPlaylist> searchPlaylists(const std::string& query, int limit = 20);

    std::optional<Models::StreamingTrack> getTrack(const std::string& trackId) override;
    TidalTrack getTidalTrack(const std::string& trackId);
    std::string getStreamQuality(const std::string& trackId);
    std::string getStreamUrl(const std::string& trackId, TidalQuality quality = TidalQuality::Lossless);

    TidalAlbum getAlbum(const std::string& albumId);
    std::vector<TidalTrack> getAlbumTracks(const std::string& albumId);

    TidalArtist getArtist(const std::string& artistId);
    std::vector<TidalTrack> getArtistTopTracks(const std::string& artistId, int limit = 20);
    std::vector<TidalAlbum> getArtistAlbums(const std::string& artistId, int limit = 20);
    std::vector<TidalArtist> getSimilarArtists(const std::string& artistId, int limit = 20);

    std::vector<Models::Playlist> getPlaylists() override;
    std::vector<TidalPlaylist> getUserPlaylists();
    TidalPlaylist getPlaylistDetails(const std::string& playlistUuid);
    std::vector<TidalTrack> getPlaylistTracks(const std::string& playlistUuid, int limit = 100, int offset = 0);
    std::string createPlaylist(const std::string& title, const std::string& description = "");
    bool addTracksToPlaylist(const std::string& playlistUuid, const std::vector<std::string>& trackIds);

    std::vector<TidalTrack> getFavoriteTracks(int limit = 100, int offset = 0);
    std::vector<TidalAlbum> getFavoriteAlbums(int limit = 100);
    bool addFavoriteTrack(const std::string& trackId);
    bool removeFavoriteTrack(const std::string& trackId);

    std::vector<TidalTrack> getMixTracks(const std::string& mixId);
    std::vector<TidalPlaylist> getEditorialPlaylists(int limit = 20);

    void setPreferredQuality(TidalQuality quality) { preferredQuality_ = quality; }
    TidalQuality getPreferredQuality() const { return preferredQuality_; }
    static std::string qualityToString(TidalQuality q);

private:
    std::string makeApiRequest(const std::string& endpoint);
    std::string makeApiPostRequest(const std::string& endpoint, const std::string& body = "");
    std::string makeApiDeleteRequest(const std::string& endpoint);

    TidalTrack parseTrack(const nlohmann::json& j) const;
    TidalAlbum parseAlbum(const nlohmann::json& j) const;
    TidalPlaylist parsePlaylist(const nlohmann::json& j) const;
    TidalArtist parseArtist(const nlohmann::json& j) const;

    std::string clientId_;
    std::string countryCode_ = "US";
    int64_t userId_ = 0;
    TidalQuality preferredQuality_ = TidalQuality::Lossless;

    static constexpr const char* API_BASE = "https://api.tidal.com/v1";
    static constexpr const char* AUTH_BASE = "https://auth.tidal.com/v1/oauth2";
};

}
