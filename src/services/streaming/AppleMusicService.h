#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "StreamingServiceBase.h"

namespace BeatMate::Services::Streaming {

struct AppleMusicSong {
    std::string id;
    std::string name;
    std::string artistName;
    std::string albumName;
    std::string isrc;
    int durationMs = 0;
    int trackNumber = 0;
    int discNumber = 1;
    std::string releaseDate;
    std::string artworkUrl;
    std::string previewUrl;
    std::string genreName;
    bool isExplicit = false;
    int playCount = 0;
    std::string composerName;
    std::string contentRating;
};

struct AppleMusicPlaylist {
    std::string id;
    std::string name;
    std::string description;
    std::string curatorName;
    std::string artworkUrl;
    bool isPublic = false;
    int trackCount = 0;
    std::string lastModified;
    std::vector<AppleMusicSong> tracks;
};

struct AppleMusicAlbum {
    std::string id;
    std::string name;
    std::string artistName;
    std::string artworkUrl;
    std::string releaseDate;
    int trackCount = 0;
    std::string genreName;
    std::string recordLabel;
    std::string copyright;
    bool isComplete = true;
};

class AppleMusicService : public StreamingServiceBase {
public:
    AppleMusicService();
    ~AppleMusicService() override = default;

    bool authenticate(const std::string& clientId, const std::string& redirectUri,
                      const std::vector<std::string>& scopes) override;
    bool refreshAccessToken() override;
    void setDeveloperToken(const std::string& jwt) { developerToken_ = jwt; }
    void setMusicUserToken(const std::string& token) { musicUserToken_ = token; }
    void setStorefront(const std::string& storefront) { storefront_ = storefront; }

    StreamingSearchResult search(const std::string& query, int limit = 20, int offset = 0) override;
    std::vector<AppleMusicSong> searchSongs(const std::string& query, int limit = 25, int offset = 0);
    std::vector<AppleMusicAlbum> searchAlbums(const std::string& query, int limit = 25);

    std::optional<Models::StreamingTrack> getTrack(const std::string& trackId) override;
    AppleMusicSong getSong(const std::string& songId);
    std::vector<AppleMusicSong> getMultipleSongs(const std::vector<std::string>& songIds);
    AppleMusicAlbum getAlbum(const std::string& albumId);
    std::vector<AppleMusicSong> getAlbumTracks(const std::string& albumId);

    std::vector<AppleMusicSong> getCharts(const std::string& genre = "", int limit = 50);

    std::vector<Models::Playlist> getPlaylists() override;
    std::vector<AppleMusicPlaylist> getAppleMusicPlaylists();
    std::vector<AppleMusicPlaylist> getCuratedPlaylists(const std::string& genre = "", int limit = 20);
    AppleMusicPlaylist getPlaylistDetails(const std::string& playlistId);
    std::vector<AppleMusicSong> getPlaylistTracks(const std::string& playlistId);

    std::vector<AppleMusicSong> getLibrarySongs(int limit = 100, int offset = 0);
    std::vector<AppleMusicPlaylist> getLibraryPlaylists(int limit = 100, int offset = 0);
    std::string createLibraryPlaylist(const std::string& name, const std::string& description = "");
    bool addSongsToLibrary(const std::vector<std::string>& songIds);
    bool addTracksToPlaylist(const std::string& playlistId, const std::vector<std::string>& songIds);

    std::vector<AppleMusicSong> getRecommendations(int limit = 20);
    std::vector<AppleMusicAlbum> getRecentlyAdded(int limit = 20);

    static std::string formatArtworkUrl(const std::string& templateUrl, int width, int height);

private:
    std::string makeApiRequest(const std::string& endpoint);
    std::string makeApiPostRequest(const std::string& endpoint, const std::string& body);

    AppleMusicSong parseSong(const nlohmann::json& item) const;
    AppleMusicAlbum parseAlbum(const nlohmann::json& item) const;
    AppleMusicPlaylist parsePlaylist(const nlohmann::json& item) const;

    std::string developerToken_;
    std::string musicUserToken_;
    std::string storefront_ = "us";

    static constexpr const char* API_BASE = "https://api.music.apple.com/v1";
};

}
