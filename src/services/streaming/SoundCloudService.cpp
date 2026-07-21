#include "SoundCloudService.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <juce_core/juce_core.h>

using json = nlohmann::json;

namespace BeatMate::Services::Streaming {

SoundCloudService::SoundCloudService()
    : StreamingServiceBase("SoundCloud", Models::StreamingServiceType::SoundCloud) {
    setRateLimit(15);
}

bool SoundCloudService::authenticate(const std::string& clientId, const std::string& redirectUri,
                                      const std::vector<std::string>& scopes) {
    clientId_ = clientId;
    redirectUri_ = redirectUri;
    if (!scopes.empty()) clientSecret_ = scopes[0];
    spdlog::info("SoundCloudService: Authenticating with client_id {}", clientId_.substr(0, 8));
    return true;
}

bool SoundCloudService::refreshAccessToken() {
    auto token = getToken();
    if (token.refreshToken.empty()) return false;
    canMakeRequest();

    juce::String postData =
        "grant_type=refresh_token"
        "&client_id=" + juce::URL::addEscapeChars(clientId_, true) +
        "&client_secret=" + juce::URL::addEscapeChars(clientSecret_, true) +
        "&refresh_token=" + juce::URL::addEscapeChars(token.refreshToken, true);

    auto httpResp = httpSend(std::string(API_BASE) + "/oauth2/token", "POST",
                             postData.toStdString(),
                             "Content-Type: application/x-www-form-urlencoded");
    if (!httpResp.ok()) {
        spdlog::error("SoundCloudService: Token refresh failed (status={})", httpResp.status);
        return false;
    }

    try {
        auto resp = json::parse(httpResp.body);
        OAuthToken newToken;
        newToken.accessToken = resp["access_token"].get<std::string>();
        newToken.refreshToken = resp.value("refresh_token", token.refreshToken);
        newToken.tokenType = resp.value("token_type", "Bearer");
        int expiresIn = resp.value("expires_in", 3600);
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        newToken.expiresAt = now + expiresIn;
        setToken(newToken);
        spdlog::info("SoundCloudService: Token refreshed");
        return true;
    } catch (const std::exception& e) {
        spdlog::error("SoundCloudService: Token refresh parse error: {}", e.what());
        return false;
    }
}

bool SoundCloudService::exchangeCode(const std::string& code) {
    juce::String postData =
        "grant_type=authorization_code"
        "&client_id=" + juce::URL::addEscapeChars(clientId_, true) +
        "&client_secret=" + juce::URL::addEscapeChars(clientSecret_, true) +
        "&redirect_uri=" + juce::URL::addEscapeChars(redirectUri_, true) +
        "&code=" + juce::URL::addEscapeChars(code, true);

    auto httpResp = httpSend(std::string(API_BASE) + "/oauth2/token", "POST",
                             postData.toStdString(),
                             "Content-Type: application/x-www-form-urlencoded");
    if (!httpResp.ok()) {
        spdlog::error("SoundCloudService: Code exchange failed (status={})", httpResp.status);
        return false;
    }

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
        spdlog::info("SoundCloudService: Authenticated successfully");
        return true;
    } catch (const std::exception& e) {
        spdlog::error("SoundCloudService: Code exchange parse error: {}", e.what());
        return false;
    }
}

StreamingSearchResult SoundCloudService::search(const std::string& query, int limit, int offset) {
    StreamingSearchResult result;
    auto tracks = searchTracks(query, limit, offset);
    for (const auto& sc : tracks) {
        Models::StreamingTrack t;
        t.serviceType = Models::StreamingServiceType::SoundCloud;
        t.serviceId = std::to_string(sc.id);
        t.durationMs = sc.durationMs;
        t.previewUrl = sc.streamUrl;
        t.artworkUrl = sc.artworkUrl;
        t.externalUrl = sc.permalinkUrl;
        t.source = Models::TrackSource::Streaming;
        result.tracks.push_back(t);
    }
    result.offset = offset;
    result.limit = limit;
    return result;
}

std::vector<SoundCloudTrack> SoundCloudService::searchTracks(const std::string& query, int limit, int offset) {
    std::vector<SoundCloudTrack> tracks;
    if (!isAuthenticated() && clientId_.empty()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_V2_BASE) + "/search/tracks?q=" +
        juce::URL::addEscapeChars(query, true).toStdString() +
        "&limit=" + std::to_string(limit) +
        "&offset=" + std::to_string(offset) +
        "&client_id=" + clientId_);

    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        auto& collection = j.contains("collection") ? j["collection"] : j;
        if (collection.is_array()) {
            for (const auto& item : collection) {
                tracks.push_back(parseTrack(item));
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("SoundCloudService: searchTracks parse error: {}", e.what());
    }

    spdlog::info("SoundCloudService: Search '{}' returned {} tracks", query, tracks.size());
    return tracks;
}

std::vector<SoundCloudPlaylist> SoundCloudService::searchPlaylists(const std::string& query, int limit) {
    std::vector<SoundCloudPlaylist> playlists;
    if (clientId_.empty()) return playlists;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_V2_BASE) + "/search/playlists?q=" +
        juce::URL::addEscapeChars(query, true).toStdString() +
        "&limit=" + std::to_string(limit) + "&client_id=" + clientId_);

    if (response.empty()) return playlists;

    try {
        auto j = json::parse(response);
        auto& collection = j.contains("collection") ? j["collection"] : j;
        if (collection.is_array()) {
            for (const auto& item : collection) {
                playlists.push_back(parsePlaylist(item));
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("SoundCloudService: searchPlaylists parse error: {}", e.what());
    }
    return playlists;
}

std::vector<SoundCloudUser> SoundCloudService::searchUsers(const std::string& query, int limit) {
    std::vector<SoundCloudUser> users;
    if (clientId_.empty()) return users;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_V2_BASE) + "/search/users?q=" +
        juce::URL::addEscapeChars(query, true).toStdString() +
        "&limit=" + std::to_string(limit) + "&client_id=" + clientId_);

    if (response.empty()) return users;

    try {
        auto j = json::parse(response);
        auto& collection = j.contains("collection") ? j["collection"] : j;
        if (collection.is_array()) {
            for (const auto& item : collection) {
                users.push_back(parseUser(item));
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("SoundCloudService: searchUsers parse error: {}", e.what());
    }
    return users;
}

std::optional<Models::StreamingTrack> SoundCloudService::getTrack(const std::string& trackId) {
    auto sc = getSoundCloudTrack(trackId);
    if (sc.id == 0) return std::nullopt;

    Models::StreamingTrack t;
    t.serviceType = Models::StreamingServiceType::SoundCloud;
    t.serviceId = std::to_string(sc.id);
    t.durationMs = sc.durationMs;
    t.previewUrl = sc.streamUrl;
    t.artworkUrl = sc.artworkUrl;
    t.externalUrl = sc.permalinkUrl;
    t.source = Models::TrackSource::Streaming;
    return t;
}

SoundCloudTrack SoundCloudService::getSoundCloudTrack(const std::string& trackId) {
    SoundCloudTrack track;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_V2_BASE) + "/tracks/" + trackId + "?client_id=" + clientId_);
    if (response.empty()) return track;

    try {
        track = parseTrack(json::parse(response));
    } catch (const std::exception& e) {
        spdlog::error("SoundCloudService: getSoundCloudTrack parse error: {}", e.what());
    }
    return track;
}

std::vector<SoundCloudTrack> SoundCloudService::getRelatedTracks(const std::string& trackId, int limit) {
    std::vector<SoundCloudTrack> tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_V2_BASE) + "/tracks/" + trackId + "/related?limit=" +
        std::to_string(limit) + "&client_id=" + clientId_);
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        auto& collection = j.contains("collection") ? j["collection"] : j;
        if (collection.is_array()) {
            for (const auto& item : collection) {
                tracks.push_back(parseTrack(item));
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("SoundCloudService: getRelatedTracks parse error: {}", e.what());
    }
    return tracks;
}

std::string SoundCloudService::getStreamUrl(const std::string& trackId) {
    canMakeRequest();
    std::string response = makeApiRequest(
        std::string(API_BASE) + "/tracks/" + trackId + "/stream?client_id=" + clientId_);
    return response;
}

std::vector<Models::Playlist> SoundCloudService::getPlaylists() {
    std::vector<Models::Playlist> playlists;
    auto scPlaylists = getUserPlaylists();
    for (const auto& scp : scPlaylists) {
        Models::Playlist pl;
        pl.name = scp.title;
        pl.description = scp.description;
        playlists.push_back(pl);
    }
    return playlists;
}

std::vector<SoundCloudPlaylist> SoundCloudService::getUserPlaylists() {
    std::vector<SoundCloudPlaylist> playlists;
    if (!isAuthenticated()) return playlists;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_V2_BASE) + "/me/playlists?limit=200");
    if (response.empty()) return playlists;

    try {
        auto j = json::parse(response);
        auto& collection = j.contains("collection") ? j["collection"] : j;
        if (collection.is_array()) {
            for (const auto& item : collection) {
                playlists.push_back(parsePlaylist(item));
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("SoundCloudService: getUserPlaylists parse error: {}", e.what());
    }
    return playlists;
}

SoundCloudPlaylist SoundCloudService::getPlaylistDetails(const std::string& playlistId) {
    SoundCloudPlaylist pl;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_V2_BASE) + "/playlists/" + playlistId + "?client_id=" + clientId_);
    if (response.empty()) return pl;

    try {
        pl = parsePlaylist(json::parse(response));
    } catch (const std::exception& e) {
        spdlog::error("SoundCloudService: getPlaylistDetails parse error: {}", e.what());
    }
    return pl;
}

std::vector<SoundCloudTrack> SoundCloudService::getPlaylistTracks(const std::string& playlistId) {
    auto pl = getPlaylistDetails(playlistId);
    return pl.tracks;
}

std::string SoundCloudService::createPlaylist(const std::string& title, const std::string& description,
                                                bool isPublic) {
    if (!isAuthenticated()) return "";
    canMakeRequest();

    json body = {
        {"playlist", {
            {"title", title},
            {"description", description},
            {"sharing", isPublic ? "public" : "private"}
        }}
    };

    std::string response = makeApiPostRequest(std::string(API_BASE) + "/playlists", body.dump());
    if (response.empty()) return "";

    try {
        auto j = json::parse(response);
        auto id = j.value("id", static_cast<int64_t>(0));
        spdlog::info("SoundCloudService: Created playlist '{}' ({})", title, id);
        return std::to_string(id);
    } catch (...) { return ""; }
}

bool SoundCloudService::addTrackToPlaylist(const std::string& playlistId, const std::string& trackId) {
    if (!isAuthenticated()) return false;
    canMakeRequest();

    json body = {{"track", {{"id", std::stoll(trackId)}}}};
    makeApiPutRequest(
        std::string(API_BASE) + "/playlists/" + playlistId, body.dump());
    spdlog::info("SoundCloudService: Added track {} to playlist {}", trackId, playlistId);
    return true;
}

SoundCloudUser SoundCloudService::getCurrentUser() {
    SoundCloudUser user;
    if (!isAuthenticated()) return user;
    canMakeRequest();

    std::string response = makeApiRequest(std::string(API_V2_BASE) + "/me");
    if (response.empty()) return user;

    try {
        user = parseUser(json::parse(response));
    } catch (const std::exception& e) {
        spdlog::error("SoundCloudService: getCurrentUser parse error: {}", e.what());
    }
    return user;
}

SoundCloudUser SoundCloudService::getUser(const std::string& userId) {
    SoundCloudUser user;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_V2_BASE) + "/users/" + userId + "?client_id=" + clientId_);
    if (response.empty()) return user;

    try {
        user = parseUser(json::parse(response));
    } catch (...) {}
    return user;
}

std::vector<SoundCloudTrack> SoundCloudService::getUserTracks(const std::string& userId, int limit) {
    std::vector<SoundCloudTrack> tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_V2_BASE) + "/users/" + userId + "/tracks?limit=" +
        std::to_string(limit) + "&client_id=" + clientId_);
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        auto& collection = j.contains("collection") ? j["collection"] : j;
        if (collection.is_array()) {
            for (const auto& item : collection) tracks.push_back(parseTrack(item));
        }
    } catch (...) {}
    return tracks;
}

std::vector<SoundCloudTrack> SoundCloudService::getUserLikes(int limit) {
    std::vector<SoundCloudTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_V2_BASE) + "/me/likes?limit=" + std::to_string(limit));
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        auto& collection = j.contains("collection") ? j["collection"] : j;
        if (collection.is_array()) {
            for (const auto& item : collection) {
                if (item.contains("track")) tracks.push_back(parseTrack(item["track"]));
            }
        }
    } catch (...) {}
    return tracks;
}

std::vector<SoundCloudTrack> SoundCloudService::getUserReposts(int limit) {
    std::vector<SoundCloudTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_V2_BASE) + "/me/reposts?limit=" + std::to_string(limit));
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        auto& collection = j.contains("collection") ? j["collection"] : j;
        if (collection.is_array()) {
            for (const auto& item : collection) {
                if (item.contains("track")) tracks.push_back(parseTrack(item["track"]));
            }
        }
    } catch (...) {}
    return tracks;
}

std::vector<SoundCloudTrack> SoundCloudService::getStreamFeed(int limit) {
    std::vector<SoundCloudTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_V2_BASE) + "/stream?limit=" + std::to_string(limit));
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("collection") && j["collection"].is_array()) {
            for (const auto& item : j["collection"]) {
                if (item.contains("track")) tracks.push_back(parseTrack(item["track"]));
            }
        }
    } catch (...) {}
    return tracks;
}

std::vector<SoundCloudTrack> SoundCloudService::getChart(const std::string& kind,
                                                           const std::string& genre, int limit) {
    std::vector<SoundCloudTrack> tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_V2_BASE) + "/charts?kind=" + kind + "&genre=soundcloud:genres:" + genre +
        "&limit=" + std::to_string(limit) + "&client_id=" + clientId_);
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("collection") && j["collection"].is_array()) {
            for (const auto& item : j["collection"]) {
                if (item.contains("track")) tracks.push_back(parseTrack(item["track"]));
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("SoundCloudService: getChart parse error: {}", e.what());
    }

    spdlog::info("SoundCloudService: Chart '{}:{}' returned {} tracks", kind, genre, tracks.size());
    return tracks;
}

bool SoundCloudService::likeTrack(const std::string& trackId) {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    makeApiPutRequest(std::string(API_V2_BASE) + "/me/likes/tracks/" + trackId);
    return true;
}

bool SoundCloudService::unlikeTrack(const std::string& trackId) {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    makeApiDeleteRequest(std::string(API_V2_BASE) + "/me/likes/tracks/" + trackId);
    return true;
}

bool SoundCloudService::followUser(const std::string& userId) {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    makeApiPutRequest(std::string(API_V2_BASE) + "/me/followings/" + userId);
    return true;
}

bool SoundCloudService::unfollowUser(const std::string& userId) {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    makeApiDeleteRequest(std::string(API_V2_BASE) + "/me/followings/" + userId);
    return true;
}

bool SoundCloudService::repostTrack(const std::string& trackId) {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    makeApiPutRequest(std::string(API_V2_BASE) + "/me/track_reposts/" + trackId);
    return true;
}

SoundCloudTrack SoundCloudService::parseTrack(const json& j) const {
    SoundCloudTrack t;
    t.id = j.value("id", static_cast<int64_t>(0));
    t.title = j.value("title", "");
    t.permalink = j.value("permalink", "");
    t.permalinkUrl = j.value("permalink_url", "");
    t.streamUrl = j.value("stream_url", "");
    t.artworkUrl = j.value("artwork_url", "");
    t.waveformUrl = j.value("waveform_url", "");
    t.genre = j.value("genre", "");
    t.description = j.value("description", "");
    t.tagList = j.value("tag_list", "");
    t.durationMs = j.value("duration", 0);
    t.playbackCount = j.value("playback_count", 0);
    t.likesCount = j.value("likes_count", j.value("favoritings_count", 0));
    t.repostsCount = j.value("reposts_count", 0);
    t.commentCount = j.value("comment_count", 0);
    t.downloadable = j.value("downloadable", false);
    t.downloadUrl = j.value("download_url", "");
    t.license = j.value("license", "");
    t.bpm = static_cast<int>(j.value("bpm", 0.0));
    t.keySignature = j.value("key_signature", "");
    t.createdAt = j.value("created_at", "");
    t.isPublic = (j.value("sharing", "public") == "public");

    if (j.contains("user")) {
        t.artistName = j["user"].value("username", "");
    }
    return t;
}

SoundCloudPlaylist SoundCloudService::parsePlaylist(const json& j) const {
    SoundCloudPlaylist pl;
    pl.id = j.value("id", static_cast<int64_t>(0));
    pl.title = j.value("title", "");
    pl.description = j.value("description", "");
    pl.artworkUrl = j.value("artwork_url", "");
    pl.permalink = j.value("permalink", "");
    pl.trackCount = j.value("track_count", 0);
    pl.durationMs = j.value("duration", 0);
    pl.genre = j.value("genre", "");
    pl.createdAt = j.value("created_at", "");
    pl.isPublic = (j.value("sharing", "public") == "public");

    if (j.contains("tracks") && j["tracks"].is_array()) {
        for (const auto& item : j["tracks"]) {
            pl.tracks.push_back(parseTrack(item));
        }
    }
    return pl;
}

SoundCloudUser SoundCloudService::parseUser(const json& j) const {
    SoundCloudUser u;
    u.id = j.value("id", static_cast<int64_t>(0));
    u.username = j.value("username", "");
    u.fullName = j.value("full_name", "");
    u.avatarUrl = j.value("avatar_url", "");
    u.city = j.value("city", "");
    u.country = j.value("country_code", j.value("country", ""));
    u.trackCount = j.value("track_count", 0);
    u.followersCount = j.value("followers_count", 0);
    u.followingsCount = j.value("followings_count", 0);
    u.description = j.value("description", "");
    return u;
}

std::string SoundCloudService::makeApiRequest(const std::string& endpoint) {
    juce::String headers;
    if (isAuthenticated()) {
        headers = "Authorization: " + juce::String(getAuthHeader());
    }

    auto resp = httpGet(endpoint, headers);
    if (!resp.ok()) {
        spdlog::error("SoundCloudService: API error: status={} for {}", resp.status, endpoint);
        return {};
    }
    return resp.body;
}

std::string SoundCloudService::makeApiPostRequest(const std::string& endpoint, const std::string& body) {
    juce::String headers = "Content-Type: application/json";
    if (isAuthenticated()) {
        headers += "\r\nAuthorization: " + juce::String(getAuthHeader());
    }

    auto resp = httpSend(endpoint, "POST", body, headers);
    return resp.ok() ? resp.body : std::string{};
}

std::string SoundCloudService::makeApiPutRequest(const std::string& endpoint, const std::string& body) {
    juce::String headers = "Content-Type: application/json";
    if (isAuthenticated()) headers += "\r\nAuthorization: " + juce::String(getAuthHeader());

    auto resp = httpSend(endpoint, "PUT", body, headers);
    return resp.ok() ? resp.body : std::string{};
}

std::string SoundCloudService::makeApiDeleteRequest(const std::string& endpoint) {
    juce::String headers;
    if (isAuthenticated()) headers = "Authorization: " + juce::String(getAuthHeader());

    auto resp = httpSend(endpoint, "DELETE", {}, headers);
    return resp.ok() ? resp.body : std::string{};
}

}
