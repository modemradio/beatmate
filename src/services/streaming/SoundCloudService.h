#pragma once
#include <optional>
#include <string>
#include <vector>
#include "StreamingServiceBase.h"

namespace BeatMate::Services::Streaming {

struct SoundCloudTrack {
    int64_t id = 0;
    std::string title;
    std::string artistName;
    std::string permalink;
    std::string permalinkUrl;
    std::string streamUrl;
    std::string artworkUrl;
    std::string waveformUrl;
    std::string genre;
    std::string description;
    std::string tagList;
    int durationMs = 0;
    int playbackCount = 0;
    int likesCount = 0;
    int repostsCount = 0;
    int commentCount = 0;
    bool downloadable = false;
    std::string downloadUrl;
    std::string license;
    int bpm = 0;
    std::string keySignature;
    std::string createdAt;
    bool isPublic = true;
};

struct SoundCloudPlaylist {
    int64_t id = 0;
    std::string title;
    std::string description;
    std::string artworkUrl;
    std::string permalink;
    int trackCount = 0;
    int durationMs = 0;
    std::string genre;
    std::string createdAt;
    bool isPublic = true;
    std::vector<SoundCloudTrack> tracks;
};

struct SoundCloudUser {
    int64_t id = 0;
    std::string username;
    std::string fullName;
    std::string avatarUrl;
    std::string city;
    std::string country;
    int trackCount = 0;
    int followersCount = 0;
    int followingsCount = 0;
    std::string description;
};

class SoundCloudService : public StreamingServiceBase {
public:
    SoundCloudService();
    ~SoundCloudService() override = default;

    bool authenticate(const std::string& clientId, const std::string& redirectUri,
                      const std::vector<std::string>& scopes) override;
    bool refreshAccessToken() override;
    bool exchangeCode(const std::string& code);

    StreamingSearchResult search(const std::string& query, int limit = 20, int offset = 0) override;
    std::vector<SoundCloudTrack> searchTracks(const std::string& query, int limit = 50, int offset = 0);
    std::vector<SoundCloudPlaylist> searchPlaylists(const std::string& query, int limit = 20);
    std::vector<SoundCloudUser> searchUsers(const std::string& query, int limit = 20);

    std::optional<Models::StreamingTrack> getTrack(const std::string& trackId) override;
    SoundCloudTrack getSoundCloudTrack(const std::string& trackId);
    std::vector<SoundCloudTrack> getRelatedTracks(const std::string& trackId, int limit = 20);
    std::string getStreamUrl(const std::string& trackId);

    std::vector<Models::Playlist> getPlaylists() override;
    std::vector<SoundCloudPlaylist> getUserPlaylists();
    SoundCloudPlaylist getPlaylistDetails(const std::string& playlistId);
    std::vector<SoundCloudTrack> getPlaylistTracks(const std::string& playlistId);
    std::string createPlaylist(const std::string& title, const std::string& description = "",
                                bool isPublic = true);
    bool addTrackToPlaylist(const std::string& playlistId, const std::string& trackId);

    SoundCloudUser getCurrentUser();
    SoundCloudUser getUser(const std::string& userId);
    std::vector<SoundCloudTrack> getUserTracks(const std::string& userId, int limit = 50);
    std::vector<SoundCloudTrack> getUserLikes(int limit = 50);
    std::vector<SoundCloudTrack> getUserReposts(int limit = 50);

    std::vector<SoundCloudTrack> getStreamFeed(int limit = 50);
    std::vector<SoundCloudTrack> getChart(const std::string& kind = "top",
                                           const std::string& genre = "all-music",
                                           int limit = 50);

    bool likeTrack(const std::string& trackId);
    bool unlikeTrack(const std::string& trackId);
    bool followUser(const std::string& userId);
    bool unfollowUser(const std::string& userId);
    bool repostTrack(const std::string& trackId);

private:
    std::string makeApiRequest(const std::string& endpoint);
    std::string makeApiPostRequest(const std::string& endpoint, const std::string& body = "");
    std::string makeApiPutRequest(const std::string& endpoint, const std::string& body = "");
    std::string makeApiDeleteRequest(const std::string& endpoint);

    SoundCloudTrack parseTrack(const nlohmann::json& j) const;
    SoundCloudPlaylist parsePlaylist(const nlohmann::json& j) const;
    SoundCloudUser parseUser(const nlohmann::json& j) const;

    std::string clientId_;
    std::string clientSecret_;
    std::string redirectUri_;

    static constexpr const char* API_BASE = "https://api.soundcloud.com";
    static constexpr const char* API_V2_BASE = "https://api-v2.soundcloud.com";
};

} // namespace BeatMate::Services::Streaming
