#include "YouTubeMusicService.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <juce_core/juce_core.h>

#include <regex>
#include <sstream>

using json = nlohmann::json;

namespace BeatMate::Services::Streaming {

YouTubeMusicService::YouTubeMusicService()
    : StreamingServiceBase("YouTube Music", Models::StreamingServiceType::YouTubeMusic) {
    setRateLimit(10);
}

bool YouTubeMusicService::authenticate(const std::string& clientId, const std::string& redirectUri,
                                        const std::vector<std::string>& scopes) {
    clientId_ = clientId;
    if (scopes.empty()) {
        apiKey_ = clientId;
        spdlog::info("YouTubeMusicService: API key set");
    } else {
        spdlog::info("YouTubeMusicService: OAuth2 flow initiated");
    }
    return true;
}

bool YouTubeMusicService::refreshAccessToken() {
    auto token = getToken();
    if (token.refreshToken.empty()) return !apiKey_.empty();
    canMakeRequest();

    juce::String postData =
        "grant_type=refresh_token"
        "&client_id=" + juce::URL::addEscapeChars(clientId_, true) +
        "&client_secret=" + juce::URL::addEscapeChars(clientSecret_, true) +
        "&refresh_token=" + juce::URL::addEscapeChars(token.refreshToken, true);

    auto httpResp = httpSend(TOKEN_URL, "POST", postData.toStdString(),
                             "Content-Type: application/x-www-form-urlencoded");
    if (!httpResp.ok()) return false;

    try {
        auto resp = json::parse(httpResp.body);
        OAuthToken newToken;
        newToken.accessToken = resp["access_token"].get<std::string>();
        newToken.refreshToken = token.refreshToken;
        newToken.tokenType = resp.value("token_type", "Bearer");
        int expiresIn = resp.value("expires_in", 3600);
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        newToken.expiresAt = now + expiresIn;
        setToken(newToken);
        spdlog::info("YouTubeMusicService: Token refreshed");
        return true;
    } catch (...) { return false; }
}

bool YouTubeMusicService::exchangeCode(const std::string& code, const std::string& clientSecret) {
    clientSecret_ = clientSecret;

    juce::String postData =
        "grant_type=authorization_code"
        "&code=" + juce::URL::addEscapeChars(code, true) +
        "&client_id=" + juce::URL::addEscapeChars(clientId_, true) +
        "&client_secret=" + juce::URL::addEscapeChars(clientSecret, true) +
        "&redirect_uri=beatmate://youtube/callback";

    auto httpResp = httpSend(TOKEN_URL, "POST", postData.toStdString(),
                             "Content-Type: application/x-www-form-urlencoded");
    if (!httpResp.ok()) return false;

    try {
        auto resp = json::parse(httpResp.body);
        OAuthToken token;
        token.accessToken = resp["access_token"].get<std::string>();
        token.refreshToken = resp.value("refresh_token", "");
        token.tokenType = resp.value("token_type", "Bearer");
        int expiresIn = resp.value("expires_in", 3600);
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        token.expiresAt = now + expiresIn;
        setToken(token);
        spdlog::info("YouTubeMusicService: Authenticated");
        return true;
    } catch (...) { return false; }
}

StreamingSearchResult YouTubeMusicService::search(const std::string& query, int limit, int offset) {
    StreamingSearchResult result;
    auto tracks = searchTracks(query, limit);
    for (const auto& yt : tracks) {
        Models::StreamingTrack t;
        t.serviceType = Models::StreamingServiceType::YouTubeMusic;
        t.serviceId = yt.videoId;
        t.durationMs = yt.durationSec * 1000;
        t.artworkUrl = yt.thumbnailUrl;
        t.externalUrl = "https://music.youtube.com/watch?v=" + yt.videoId;
        t.isExplicit = yt.isExplicit;
        t.source = Models::TrackSource::Streaming;
        result.tracks.push_back(t);
    }
    result.offset = offset;
    result.limit = limit;
    return result;
}

std::vector<YTMusicTrack> YouTubeMusicService::searchTracks(const std::string& query, int limit) {
    std::vector<YTMusicTrack> tracks;
    if (apiKey_.empty() && !isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/search?part=snippet&type=video&videoCategoryId=10&q=" +
        juce::URL::addEscapeChars(query, true).toStdString() +
        "&maxResults=" + std::to_string(limit) + "&key=" + apiKey_);

    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            std::vector<std::string> videoIds;
            for (const auto& item : j["items"]) {
                if (item.contains("id") && item["id"].contains("videoId")) {
                    videoIds.push_back(item["id"]["videoId"].get<std::string>());
                }
            }

            if (!videoIds.empty()) {
                std::string ids;
                for (size_t i = 0; i < videoIds.size(); ++i) {
                    if (i > 0) ids += ",";
                    ids += videoIds[i];
                }

                canMakeRequest();
                std::string detailResp = makeApiRequest(
                    std::string(API_BASE) + "/videos?part=snippet,contentDetails,statistics&id=" +
                    ids + "&key=" + apiKey_);

                if (!detailResp.empty()) {
                    auto dj = json::parse(detailResp);
                    if (dj.contains("items")) {
                        for (const auto& item : dj["items"]) {
                            tracks.push_back(parseTrackFromDetails(item));
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("YouTubeMusicService: searchTracks parse error: {}", e.what());
    }

    spdlog::info("YouTubeMusicService: Search '{}' returned {} tracks", query, tracks.size());
    return tracks;
}

std::vector<YTMusicPlaylist> YouTubeMusicService::searchPlaylists(const std::string& query, int limit) {
    std::vector<YTMusicPlaylist> playlists;
    if (apiKey_.empty() && !isAuthenticated()) return playlists;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/search?part=snippet&type=playlist&q=" +
        juce::URL::addEscapeChars(query, true).toStdString() +
        "&maxResults=" + std::to_string(limit) + "&key=" + apiKey_);
    if (response.empty()) return playlists;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) {
                YTMusicPlaylist pl;
                if (item.contains("id") && item["id"].contains("playlistId"))
                    pl.playlistId = item["id"]["playlistId"].get<std::string>();
                if (item.contains("snippet")) {
                    pl.title = item["snippet"].value("title", "");
                    pl.description = item["snippet"].value("description", "");
                    pl.channelName = item["snippet"].value("channelTitle", "");
                    if (item["snippet"].contains("thumbnails") &&
                        item["snippet"]["thumbnails"].contains("high")) {
                        pl.thumbnailUrl = item["snippet"]["thumbnails"]["high"].value("url", "");
                    }
                }
                playlists.push_back(pl);
            }
        }
    } catch (...) {}
    return playlists;
}

std::vector<YTMusicArtist> YouTubeMusicService::searchArtists(const std::string& query, int limit) {
    std::vector<YTMusicArtist> artists;
    if (apiKey_.empty() && !isAuthenticated()) return artists;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/search?part=snippet&type=channel&q=" +
        juce::URL::addEscapeChars(query, true).toStdString() +
        "&maxResults=" + std::to_string(limit) + "&key=" + apiKey_);
    if (response.empty()) return artists;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) {
                YTMusicArtist a;
                if (item.contains("id") && item["id"].contains("channelId"))
                    a.channelId = item["id"]["channelId"].get<std::string>();
                if (item.contains("snippet")) {
                    a.name = item["snippet"].value("title", "");
                    if (item["snippet"].contains("thumbnails") &&
                        item["snippet"]["thumbnails"].contains("default")) {
                        a.thumbnailUrl = item["snippet"]["thumbnails"]["default"].value("url", "");
                    }
                }
                artists.push_back(a);
            }
        }
    } catch (...) {}
    return artists;
}

std::optional<Models::StreamingTrack> YouTubeMusicService::getTrack(const std::string& trackId) {
    auto yt = getVideoDetails(trackId);
    if (yt.videoId.empty()) return std::nullopt;

    Models::StreamingTrack t;
    t.serviceType = Models::StreamingServiceType::YouTubeMusic;
    t.serviceId = yt.videoId;
    t.durationMs = yt.durationSec * 1000;
    t.artworkUrl = yt.thumbnailUrl;
    t.externalUrl = "https://music.youtube.com/watch?v=" + yt.videoId;
    t.source = Models::TrackSource::Streaming;
    return t;
}

YTMusicTrack YouTubeMusicService::getVideoDetails(const std::string& videoId) {
    YTMusicTrack track;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/videos?part=snippet,contentDetails,statistics&id=" +
        videoId + "&key=" + apiKey_);
    if (response.empty()) return track;

    try {
        auto j = json::parse(response);
        if (j.contains("items") && !j["items"].empty()) {
            track = parseTrackFromDetails(j["items"][0]);
        }
    } catch (const std::exception& e) {
        spdlog::error("YouTubeMusicService: getVideoDetails error: {}", e.what());
    }
    return track;
}

std::vector<YTMusicTrack> YouTubeMusicService::getRelatedVideos(const std::string& videoId, int limit) {
    std::vector<YTMusicTrack> tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/search?part=snippet&type=video&videoCategoryId=10&relatedToVideoId=" +
        videoId + "&maxResults=" + std::to_string(limit) + "&key=" + apiKey_);
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) {
                tracks.push_back(parseTrackFromSnippet(item));
            }
        }
    } catch (...) {}
    return tracks;
}

std::vector<Models::Playlist> YouTubeMusicService::getPlaylists() {
    std::vector<Models::Playlist> playlists;
    auto ytPlaylists = getUserPlaylists();
    for (const auto& ytp : ytPlaylists) {
        Models::Playlist pl;
        pl.name = ytp.title;
        pl.description = ytp.description;
        playlists.push_back(pl);
    }
    return playlists;
}

std::vector<YTMusicPlaylist> YouTubeMusicService::getUserPlaylists() {
    std::vector<YTMusicPlaylist> playlists;
    if (!isAuthenticated()) return playlists;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/playlists?part=snippet,contentDetails&mine=true&maxResults=50&key=" + apiKey_);
    if (response.empty()) return playlists;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) {
                YTMusicPlaylist pl;
                pl.playlistId = item.value("id", "");
                if (item.contains("snippet")) {
                    pl.title = item["snippet"].value("title", "");
                    pl.description = item["snippet"].value("description", "");
                    pl.channelName = item["snippet"].value("channelTitle", "");
                    if (item["snippet"].contains("thumbnails") &&
                        item["snippet"]["thumbnails"].contains("high")) {
                        pl.thumbnailUrl = item["snippet"]["thumbnails"]["high"].value("url", "");
                    }
                }
                if (item.contains("contentDetails")) {
                    pl.trackCount = item["contentDetails"].value("itemCount", 0);
                }
                if (item.contains("status")) {
                    pl.privacy = item["status"].value("privacyStatus", "PRIVATE");
                }
                playlists.push_back(pl);
            }
        }
    } catch (...) {}
    return playlists;
}

YTMusicPlaylist YouTubeMusicService::getPlaylistDetails(const std::string& playlistId) {
    YTMusicPlaylist pl;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/playlists?part=snippet,contentDetails&id=" +
        playlistId + "&key=" + apiKey_);
    if (response.empty()) return pl;

    try {
        auto j = json::parse(response);
        if (j.contains("items") && !j["items"].empty()) {
            auto& item = j["items"][0];
            pl.playlistId = item.value("id", "");
            if (item.contains("snippet")) {
                pl.title = item["snippet"].value("title", "");
                pl.description = item["snippet"].value("description", "");
                pl.channelName = item["snippet"].value("channelTitle", "");
            }
            if (item.contains("contentDetails")) {
                pl.trackCount = item["contentDetails"].value("itemCount", 0);
            }
        }
    } catch (...) {}

    pl.tracks = getPlaylistTracks(playlistId);
    return pl;
}

std::vector<YTMusicTrack> YouTubeMusicService::getPlaylistTracks(const std::string& playlistId, int limit) {
    std::vector<YTMusicTrack> tracks;
    canMakeRequest();

    std::string nextPageToken;
    int fetched = 0;

    while (fetched < limit) {
        int pageSize = std::min(50, limit - fetched);
        std::string endpoint = std::string(API_BASE) + "/playlistItems?part=snippet&playlistId=" +
                                playlistId + "&maxResults=" + std::to_string(pageSize) + "&key=" + apiKey_;
        if (!nextPageToken.empty()) endpoint += "&pageToken=" + nextPageToken;

        std::string response = makeApiRequest(endpoint);
        if (response.empty()) break;

        try {
            auto j = json::parse(response);
            if (j.contains("items")) {
                for (const auto& item : j["items"]) {
                    YTMusicTrack t;
                    if (item.contains("snippet")) {
                        auto& sn = item["snippet"];
                        t.title = sn.value("title", "");
                        t.artistName = sn.value("videoOwnerChannelTitle", "");
                        if (sn.contains("resourceId")) {
                            t.videoId = sn["resourceId"].value("videoId", "");
                        }
                        if (sn.contains("thumbnails") && sn["thumbnails"].contains("high")) {
                            t.thumbnailUrl = sn["thumbnails"]["high"].value("url", "");
                        }
                    }
                    tracks.push_back(t);
                    fetched++;
                }
            }
            nextPageToken = j.value("nextPageToken", "");
            if (nextPageToken.empty()) break;
            canMakeRequest();
        } catch (...) { break; }
    }
    return tracks;
}

std::string YouTubeMusicService::createPlaylist(const std::string& title, const std::string& description,
                                                  const std::string& privacy) {
    if (!isAuthenticated()) return "";
    canMakeRequest();

    json body = {
        {"snippet", {{"title", title}, {"description", description}}},
        {"status", {{"privacyStatus", privacy}}}
    };

    std::string response = makeApiPostRequest(
        std::string(API_BASE) + "/playlists?part=snippet,status&key=" + apiKey_, body.dump());
    if (response.empty()) return "";

    try {
        auto j = json::parse(response);
        std::string id = j.value("id", "");
        spdlog::info("YouTubeMusicService: Created playlist '{}' ({})", title, id);
        return id;
    } catch (...) { return ""; }
}

bool YouTubeMusicService::addVideosToPlaylist(const std::string& playlistId,
                                                const std::vector<std::string>& videoIds) {
    if (!isAuthenticated()) return false;

    for (const auto& videoId : videoIds) {
        canMakeRequest();
        json body = {
            {"snippet", {
                {"playlistId", playlistId},
                {"resourceId", {
                    {"kind", "youtube#video"},
                    {"videoId", videoId}
                }}
            }}
        };
        makeApiPostRequest(
            std::string(API_BASE) + "/playlistItems?part=snippet&key=" + apiKey_, body.dump());
    }
    spdlog::info("YouTubeMusicService: Added {} videos to playlist {}", videoIds.size(), playlistId);
    return true;
}

bool YouTubeMusicService::removeVideoFromPlaylist(const std::string& /*playlistId*/,
                                                    const std::string& playlistItemId) {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    makeApiDeleteRequest(std::string(API_BASE) + "/playlistItems?id=" + playlistItemId + "&key=" + apiKey_);
    return true;
}

bool YouTubeMusicService::deletePlaylist(const std::string& playlistId) {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    makeApiDeleteRequest(std::string(API_BASE) + "/playlists?id=" + playlistId + "&key=" + apiKey_);
    spdlog::info("YouTubeMusicService: Deleted playlist {}", playlistId);
    return true;
}

std::vector<YTMusicTrack> YouTubeMusicService::getLikedVideos(int limit) {
    std::vector<YTMusicTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/videos?part=snippet,contentDetails&myRating=like&maxResults=" +
        std::to_string(limit) + "&key=" + apiKey_);
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) tracks.push_back(parseTrackFromDetails(item));
        }
    } catch (...) {}
    return tracks;
}

bool YouTubeMusicService::likeVideo(const std::string& videoId) {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    makeApiPostRequest(std::string(API_BASE) + "/videos/rate?id=" + videoId + "&rating=like&key=" + apiKey_, "");
    return true;
}

bool YouTubeMusicService::dislikeVideo(const std::string& videoId) {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    makeApiPostRequest(std::string(API_BASE) + "/videos/rate?id=" + videoId + "&rating=dislike&key=" + apiKey_, "");
    return true;
}

bool YouTubeMusicService::removeRating(const std::string& videoId) {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    makeApiPostRequest(std::string(API_BASE) + "/videos/rate?id=" + videoId + "&rating=none&key=" + apiKey_, "");
    return true;
}

std::vector<YTMusicTrack> YouTubeMusicService::getTrendingMusic(const std::string& regionCode, int limit) {
    std::vector<YTMusicTrack> tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/videos?part=snippet,contentDetails,statistics&chart=mostPopular"
        "&videoCategoryId=10&regionCode=" + regionCode +
        "&maxResults=" + std::to_string(limit) + "&key=" + apiKey_);
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) tracks.push_back(parseTrackFromDetails(item));
        }
    } catch (const std::exception& e) {
        spdlog::error("YouTubeMusicService: getTrendingMusic error: {}", e.what());
    }

    spdlog::info("YouTubeMusicService: Got {} trending tracks for {}", tracks.size(), regionCode);
    return tracks;
}

std::vector<YTMusicTrack> YouTubeMusicService::getNewMusicVideos(int limit) {
    return getTrendingMusic("US", limit);
}

int YouTubeMusicService::isoDurationToSeconds(const std::string& isoDuration) {
    int hours = 0, minutes = 0, seconds = 0;
    std::regex re(R"(PT(?:(\d+)H)?(?:(\d+)M)?(?:(\d+)S)?)");
    std::smatch match;
    if (std::regex_match(isoDuration, match, re)) {
        if (match[1].matched) hours = std::stoi(match[1].str());
        if (match[2].matched) minutes = std::stoi(match[2].str());
        if (match[3].matched) seconds = std::stoi(match[3].str());
    }
    return hours * 3600 + minutes * 60 + seconds;
}

std::string YouTubeMusicService::parseDuration(const std::string& isoDuration) {
    int total = isoDurationToSeconds(isoDuration);
    int h = total / 3600;
    int m = (total % 3600) / 60;
    int s = total % 60;

    std::ostringstream oss;
    if (h > 0) oss << h << ":";
    if (h > 0 && m < 10) oss << "0";
    oss << m << ":";
    if (s < 10) oss << "0";
    oss << s;
    return oss.str();
}

YTMusicTrack YouTubeMusicService::parseTrackFromSnippet(const json& item) const {
    YTMusicTrack t;
    if (item.contains("id")) {
        if (item["id"].is_string()) t.videoId = item["id"].get<std::string>();
        else if (item["id"].contains("videoId")) t.videoId = item["id"]["videoId"].get<std::string>();
    }
    if (item.contains("snippet")) {
        auto& sn = item["snippet"];
        t.title = sn.value("title", "");
        t.artistName = sn.value("channelTitle", "");
        t.publishedAt = sn.value("publishedAt", "");
        if (sn.contains("thumbnails")) {
            if (sn["thumbnails"].contains("high"))
                t.thumbnailUrl = sn["thumbnails"]["high"].value("url", "");
            else if (sn["thumbnails"].contains("default"))
                t.thumbnailUrl = sn["thumbnails"]["default"].value("url", "");
        }
    }
    return t;
}

YTMusicTrack YouTubeMusicService::parseTrackFromDetails(const json& item) const {
    YTMusicTrack t = parseTrackFromSnippet(item);

    if (item.contains("contentDetails")) {
        std::string dur = item["contentDetails"].value("duration", "");
        t.durationSec = isoDurationToSeconds(dur);
    }
    if (item.contains("statistics")) {
        auto& stats = item["statistics"];
        t.viewCount = std::stoll(stats.value("viewCount", "0"));
        t.likeCount = std::stoll(stats.value("likeCount", "0"));
    }
    return t;
}

std::string YouTubeMusicService::makeApiRequest(const std::string& endpoint) {
    juce::String extraHeaders;
    if (isAuthenticated()) {
        extraHeaders = "Authorization: " + juce::String(getAuthHeader());
    }

    auto resp = httpGet(endpoint, extraHeaders);
    if (!resp.ok()) {
        spdlog::error("YouTubeMusicService: API error: status={} for {}", resp.status, endpoint);
        return {};
    }
    return resp.body;
}

std::string YouTubeMusicService::makeApiPostRequest(const std::string& endpoint, const std::string& body) {
    juce::String headers = "Content-Type: application/json";
    if (isAuthenticated()) headers += "\r\nAuthorization: " + juce::String(getAuthHeader());

    auto resp = httpSend(endpoint, "POST", body, headers);
    return resp.ok() ? resp.body : std::string{};
}

std::string YouTubeMusicService::makeApiDeleteRequest(const std::string& endpoint) {
    juce::String headers;
    if (isAuthenticated()) headers = "Authorization: " + juce::String(getAuthHeader());

    auto resp = httpSend(endpoint, "DELETE", {}, headers);
    return resp.ok() ? resp.body : std::string{};
}

} // namespace BeatMate::Services::Streaming
