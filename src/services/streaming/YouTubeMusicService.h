#pragma once
#include <optional>
#include <string>
#include <vector>
#include "StreamingServiceBase.h"

namespace BeatMate::Services::Streaming {

struct YTMusicTrack {
    std::string videoId;
    std::string title;
    std::string artistName;
    std::string albumName;
    std::string thumbnailUrl;
    int durationSec = 0;
    bool isExplicit = false;
    std::string category;
    int64_t viewCount = 0;
    int64_t likeCount = 0;
    std::string publishedAt;
};

struct YTMusicPlaylist {
    std::string playlistId;
    std::string title;
    std::string description;
    std::string thumbnailUrl;
    int trackCount = 0;
    std::string channelName;
    std::string privacy;
    std::vector<YTMusicTrack> tracks;
};

struct YTMusicArtist {
    std::string channelId;
    std::string name;
    std::string thumbnailUrl;
    int64_t subscriberCount = 0;
};

class YouTubeMusicService : public StreamingServiceBase {
public:
    YouTubeMusicService();
    ~YouTubeMusicService() override = default;

    bool authenticate(const std::string& clientId, const std::string& redirectUri,
                      const std::vector<std::string>& scopes) override;
    bool refreshAccessToken() override;
    bool exchangeCode(const std::string& code, const std::string& clientSecret);
    void setApiKey(const std::string& apiKey) { apiKey_ = apiKey; }

    StreamingSearchResult search(const std::string& query, int limit = 20, int offset = 0) override;
    std::vector<YTMusicTrack> searchTracks(const std::string& query, int limit = 25);
    std::vector<YTMusicPlaylist> searchPlaylists(const std::string& query, int limit = 10);
    std::vector<YTMusicArtist> searchArtists(const std::string& query, int limit = 10);

    std::optional<Models::StreamingTrack> getTrack(const std::string& trackId) override;
    YTMusicTrack getVideoDetails(const std::string& videoId);
    std::vector<YTMusicTrack> getRelatedVideos(const std::string& videoId, int limit = 20);

    std::vector<Models::Playlist> getPlaylists() override;
    std::vector<YTMusicPlaylist> getUserPlaylists();
    YTMusicPlaylist getPlaylistDetails(const std::string& playlistId);
    std::vector<YTMusicTrack> getPlaylistTracks(const std::string& playlistId, int limit = 200);
    std::string createPlaylist(const std::string& title, const std::string& description = "",
                                const std::string& privacy = "PRIVATE");
    bool addVideosToPlaylist(const std::string& playlistId, const std::vector<std::string>& videoIds);
    bool removeVideoFromPlaylist(const std::string& playlistId, const std::string& playlistItemId);
    bool deletePlaylist(const std::string& playlistId);

    std::vector<YTMusicTrack> getLikedVideos(int limit = 50);
    bool likeVideo(const std::string& videoId);
    bool dislikeVideo(const std::string& videoId);
    bool removeRating(const std::string& videoId);

    std::vector<YTMusicTrack> getTrendingMusic(const std::string& regionCode = "US", int limit = 50);
    std::vector<YTMusicTrack> getNewMusicVideos(int limit = 25);

    static std::string parseDuration(const std::string& isoDuration);
    static int isoDurationToSeconds(const std::string& isoDuration);

private:
    std::string makeApiRequest(const std::string& endpoint);
    std::string makeApiPostRequest(const std::string& endpoint, const std::string& body);
    std::string makeApiDeleteRequest(const std::string& endpoint);

    YTMusicTrack parseTrackFromSnippet(const nlohmann::json& item) const;
    YTMusicTrack parseTrackFromDetails(const nlohmann::json& item) const;

    std::string apiKey_;
    std::string clientId_;
    std::string clientSecret_;

    static constexpr const char* API_BASE = "https://www.googleapis.com/youtube/v3";
    static constexpr const char* TOKEN_URL = "https://oauth2.googleapis.com/token";
};

}
