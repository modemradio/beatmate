#pragma once
#include <optional>
#include <string>
#include <vector>
#include "StreamingServiceBase.h"

namespace BeatMate::Services::Streaming {

struct AmazonMusicTrack {
    std::string asin;
    std::string title;
    std::string artistName;
    std::string albumName;
    std::string albumAsin;
    int durationMs = 0;
    int trackNumber = 0;
    int discNumber = 1;
    std::string artworkUrl;
    bool isExplicit = false;
    bool isPrime = false;
    bool isUnlimited = false;
    std::string tier;  // "PRIME", "UNLIMITED", "PURCHASED"
};

struct AmazonMusicAlbum {
    std::string asin;
    std::string title;
    std::string artistName;
    std::string artworkUrl;
    std::string releaseDate;
    int trackCount = 0;
    std::string genre;
};

struct AmazonMusicPlaylist {
    std::string asin;
    std::string title;
    std::string description;
    std::string artworkUrl;
    int trackCount = 0;
    std::string curatorName;
    std::string visibility;  // "PUBLIC", "PRIVATE"
    std::vector<AmazonMusicTrack> tracks;
};

class AmazonMusicService : public StreamingServiceBase {
public:
    AmazonMusicService();
    ~AmazonMusicService() override = default;

    bool authenticate(const std::string& clientId, const std::string& redirectUri,
                      const std::vector<std::string>& scopes) override;
    bool refreshAccessToken() override;
    bool exchangeCode(const std::string& code, const std::string& clientSecret);

    StreamingSearchResult search(const std::string& query, int limit = 20, int offset = 0) override;
    std::vector<AmazonMusicTrack> searchTracks(const std::string& query, int limit = 25);
    std::vector<AmazonMusicAlbum> searchAlbums(const std::string& query, int limit = 25);
    std::vector<AmazonMusicPlaylist> searchPlaylists(const std::string& query, int limit = 10);

    std::optional<Models::StreamingTrack> getTrack(const std::string& trackId) override;
    AmazonMusicTrack getAmazonTrack(const std::string& asin);
    AmazonMusicAlbum getAlbum(const std::string& asin);
    std::vector<AmazonMusicTrack> getAlbumTracks(const std::string& albumAsin);

    std::vector<Models::Playlist> getPlaylists() override;
    std::vector<AmazonMusicPlaylist> getUserPlaylists();
    AmazonMusicPlaylist getPlaylistDetails(const std::string& playlistAsin);
    std::vector<AmazonMusicTrack> getPlaylistTracks(const std::string& playlistAsin);
    std::string createPlaylist(const std::string& title, const std::string& description = "");
    bool addTracksToPlaylist(const std::string& playlistAsin, const std::vector<std::string>& trackAsins);

    std::vector<AmazonMusicTrack> getLibraryTracks(int limit = 100, int offset = 0);
    std::vector<AmazonMusicAlbum> getLibraryAlbums(int limit = 100);
    std::vector<AmazonMusicTrack> getRecentlyPlayed(int limit = 50);
    bool addTrackToLibrary(const std::string& asin);

    std::vector<AmazonMusicPlaylist> getRecommendedPlaylists(int limit = 20);
    std::vector<AmazonMusicTrack> getStationTracks(const std::string& stationId, int limit = 50);

private:
    std::string makeApiRequest(const std::string& endpoint, const std::string& body = "");
    std::string makeApiPostRequest(const std::string& endpoint, const std::string& body);

    AmazonMusicTrack parseTrack(const nlohmann::json& j) const;
    AmazonMusicAlbum parseAlbum(const nlohmann::json& j) const;
    AmazonMusicPlaylist parsePlaylist(const nlohmann::json& j) const;

    std::string clientId_;
    std::string clientSecret_;
    std::string marketplaceId_ = "ATVPDKIKX0DER";  // US marketplace

    static constexpr const char* API_BASE = "https://api.music.amazon.dev/v1";
    static constexpr const char* TOKEN_URL = "https://api.amazon.com/auth/o2/token";
};

} // namespace BeatMate::Services::Streaming
