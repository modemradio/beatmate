#include "AmazonMusicService.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <juce_core/juce_core.h>

using json = nlohmann::json;

namespace BeatMate::Services::Streaming {

AmazonMusicService::AmazonMusicService()
    : StreamingServiceBase("Amazon Music", Models::StreamingServiceType::AmazonMusic) {
    setRateLimit(10);
}

bool AmazonMusicService::authenticate(const std::string& clientId, const std::string& redirectUri,
                                       const std::vector<std::string>& scopes) {
    clientId_ = clientId;
    spdlog::info("AmazonMusicService: Starting OAuth2 authentication");
    return true;
}

bool AmazonMusicService::refreshAccessToken() {
    auto token = getToken();
    if (token.refreshToken.empty()) return false;
    canMakeRequest();

    juce::String postData =
        "grant_type=refresh_token"
        "&refresh_token=" + juce::URL::addEscapeChars(token.refreshToken, true) +
        "&client_id=" + juce::URL::addEscapeChars(clientId_, true) +
        "&client_secret=" + juce::URL::addEscapeChars(clientSecret_, true);

    auto httpResp = httpSend(TOKEN_URL, "POST", postData.toStdString(),
                             "Content-Type: application/x-www-form-urlencoded");
    if (!httpResp.ok()) return false;

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
        spdlog::info("AmazonMusicService: Token refreshed");
        return true;
    } catch (...) { return false; }
}

bool AmazonMusicService::exchangeCode(const std::string& code, const std::string& clientSecret) {
    clientSecret_ = clientSecret;

    juce::String postData =
        "grant_type=authorization_code"
        "&code=" + juce::URL::addEscapeChars(code, true) +
        "&client_id=" + juce::URL::addEscapeChars(clientId_, true) +
        "&client_secret=" + juce::URL::addEscapeChars(clientSecret, true) +
        "&redirect_uri=beatmate://amazon/callback";

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
        spdlog::info("AmazonMusicService: Authenticated");
        return true;
    } catch (...) { return false; }
}

StreamingSearchResult AmazonMusicService::search(const std::string& query, int limit, int offset) {
    StreamingSearchResult result;
    auto tracks = searchTracks(query, limit);
    for (const auto& at : tracks) {
        Models::StreamingTrack t;
        t.serviceType = Models::StreamingServiceType::AmazonMusic;
        t.serviceId = at.asin;
        t.durationMs = at.durationMs;
        t.artworkUrl = at.artworkUrl;
        t.isExplicit = at.isExplicit;
        t.source = Models::TrackSource::Streaming;
        result.tracks.push_back(t);
    }
    result.offset = offset;
    result.limit = limit;
    return result;
}

std::vector<AmazonMusicTrack> AmazonMusicService::searchTracks(const std::string& query, int limit) {
    std::vector<AmazonMusicTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    json body = {
        {"keyword", query},
        {"maxResults", limit},
        {"types", {"TRACKS"}},
        {"marketplaceId", marketplaceId_}
    };

    std::string response = makeApiRequest(std::string(API_BASE) + "/search", body.dump());
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("results") && j["results"].contains("tracks")) {
            for (const auto& item : j["results"]["tracks"]) {
                tracks.push_back(parseTrack(item));
            }
        } else if (j.contains("tracks")) {
            for (const auto& item : j["tracks"]) {
                tracks.push_back(parseTrack(item));
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("AmazonMusicService: searchTracks error: {}", e.what());
    }

    spdlog::info("AmazonMusicService: Search '{}' returned {} tracks", query, tracks.size());
    return tracks;
}

std::vector<AmazonMusicAlbum> AmazonMusicService::searchAlbums(const std::string& query, int limit) {
    std::vector<AmazonMusicAlbum> albums;
    if (!isAuthenticated()) return albums;
    canMakeRequest();

    json body = {{"keyword", query}, {"maxResults", limit}, {"types", {"ALBUMS"}}};
    std::string response = makeApiRequest(std::string(API_BASE) + "/search", body.dump());
    if (response.empty()) return albums;

    try {
        auto j = json::parse(response);
        auto& results = j.contains("results") ? j["results"] : j;
        if (results.contains("albums")) {
            for (const auto& item : results["albums"]) albums.push_back(parseAlbum(item));
        }
    } catch (...) {}
    return albums;
}

std::vector<AmazonMusicPlaylist> AmazonMusicService::searchPlaylists(const std::string& query, int limit) {
    std::vector<AmazonMusicPlaylist> playlists;
    if (!isAuthenticated()) return playlists;
    canMakeRequest();

    json body = {{"keyword", query}, {"maxResults", limit}, {"types", {"PLAYLISTS"}}};
    std::string response = makeApiRequest(std::string(API_BASE) + "/search", body.dump());
    if (response.empty()) return playlists;

    try {
        auto j = json::parse(response);
        auto& results = j.contains("results") ? j["results"] : j;
        if (results.contains("playlists")) {
            for (const auto& item : results["playlists"]) playlists.push_back(parsePlaylist(item));
        }
    } catch (...) {}
    return playlists;
}

std::optional<Models::StreamingTrack> AmazonMusicService::getTrack(const std::string& trackId) {
    auto at = getAmazonTrack(trackId);
    if (at.asin.empty()) return std::nullopt;

    Models::StreamingTrack t;
    t.serviceType = Models::StreamingServiceType::AmazonMusic;
    t.serviceId = at.asin;
    t.durationMs = at.durationMs;
    t.artworkUrl = at.artworkUrl;
    t.isExplicit = at.isExplicit;
    t.source = Models::TrackSource::Streaming;
    return t;
}

AmazonMusicTrack AmazonMusicService::getAmazonTrack(const std::string& asin) {
    AmazonMusicTrack track;
    if (!isAuthenticated()) return track;
    canMakeRequest();

    std::string response = makeApiRequest(std::string(API_BASE) + "/tracks/" + asin);
    if (response.empty()) return track;

    try {
        track = parseTrack(json::parse(response));
    } catch (...) {}
    return track;
}

AmazonMusicAlbum AmazonMusicService::getAlbum(const std::string& asin) {
    AmazonMusicAlbum album;
    if (!isAuthenticated()) return album;
    canMakeRequest();

    std::string response = makeApiRequest(std::string(API_BASE) + "/albums/" + asin);
    if (response.empty()) return album;
    try { album = parseAlbum(json::parse(response)); } catch (...) {}
    return album;
}

std::vector<AmazonMusicTrack> AmazonMusicService::getAlbumTracks(const std::string& albumAsin) {
    std::vector<AmazonMusicTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(std::string(API_BASE) + "/albums/" + albumAsin + "/tracks");
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("tracks") && j["tracks"].is_array()) {
            for (const auto& item : j["tracks"]) tracks.push_back(parseTrack(item));
        }
    } catch (...) {}
    return tracks;
}

std::vector<Models::Playlist> AmazonMusicService::getPlaylists() {
    std::vector<Models::Playlist> playlists;
    auto amPlaylists = getUserPlaylists();
    for (const auto& ap : amPlaylists) {
        Models::Playlist pl;
        pl.name = ap.title;
        pl.description = ap.description;
        playlists.push_back(pl);
    }
    return playlists;
}

std::vector<AmazonMusicPlaylist> AmazonMusicService::getUserPlaylists() {
    std::vector<AmazonMusicPlaylist> playlists;
    if (!isAuthenticated()) return playlists;
    canMakeRequest();

    std::string response = makeApiRequest(std::string(API_BASE) + "/me/playlists");
    if (response.empty()) return playlists;

    try {
        auto j = json::parse(response);
        if (j.contains("playlists") && j["playlists"].is_array()) {
            for (const auto& item : j["playlists"]) playlists.push_back(parsePlaylist(item));
        }
    } catch (...) {}
    return playlists;
}

AmazonMusicPlaylist AmazonMusicService::getPlaylistDetails(const std::string& playlistAsin) {
    AmazonMusicPlaylist pl;
    if (!isAuthenticated()) return pl;
    canMakeRequest();

    std::string response = makeApiRequest(std::string(API_BASE) + "/playlists/" + playlistAsin);
    if (response.empty()) return pl;
    try { pl = parsePlaylist(json::parse(response)); } catch (...) {}
    pl.tracks = getPlaylistTracks(playlistAsin);
    return pl;
}

std::vector<AmazonMusicTrack> AmazonMusicService::getPlaylistTracks(const std::string& playlistAsin) {
    std::vector<AmazonMusicTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(std::string(API_BASE) + "/playlists/" + playlistAsin + "/tracks");
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("tracks") && j["tracks"].is_array()) {
            for (const auto& item : j["tracks"]) tracks.push_back(parseTrack(item));
        }
    } catch (...) {}
    return tracks;
}

std::string AmazonMusicService::createPlaylist(const std::string& title, const std::string& description) {
    if (!isAuthenticated()) return "";
    canMakeRequest();

    json body = {{"title", title}, {"description", description}, {"visibility", "PRIVATE"}};
    std::string response = makeApiPostRequest(std::string(API_BASE) + "/me/playlists", body.dump());
    if (response.empty()) return "";

    try {
        auto j = json::parse(response);
        std::string asin = j.value("asin", "");
        spdlog::info("AmazonMusicService: Created playlist '{}' ({})", title, asin);
        return asin;
    } catch (...) { return ""; }
}

bool AmazonMusicService::addTracksToPlaylist(const std::string& playlistAsin,
                                               const std::vector<std::string>& trackAsins) {
    if (!isAuthenticated() || trackAsins.empty()) return false;
    canMakeRequest();

    json body = {{"trackAsins", trackAsins}};
    makeApiPostRequest(
        std::string(API_BASE) + "/playlists/" + playlistAsin + "/tracks", body.dump());
    spdlog::info("AmazonMusicService: Added {} tracks to playlist {}", trackAsins.size(), playlistAsin);
    return true;
}

std::vector<AmazonMusicTrack> AmazonMusicService::getLibraryTracks(int limit, int offset) {
    std::vector<AmazonMusicTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/me/library/tracks?maxResults=" + std::to_string(limit) +
        "&offset=" + std::to_string(offset));
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("tracks") && j["tracks"].is_array()) {
            for (const auto& item : j["tracks"]) tracks.push_back(parseTrack(item));
        }
    } catch (...) {}
    return tracks;
}

std::vector<AmazonMusicAlbum> AmazonMusicService::getLibraryAlbums(int limit) {
    std::vector<AmazonMusicAlbum> albums;
    if (!isAuthenticated()) return albums;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/me/library/albums?maxResults=" + std::to_string(limit));
    if (response.empty()) return albums;

    try {
        auto j = json::parse(response);
        if (j.contains("albums") && j["albums"].is_array()) {
            for (const auto& item : j["albums"]) albums.push_back(parseAlbum(item));
        }
    } catch (...) {}
    return albums;
}

std::vector<AmazonMusicTrack> AmazonMusicService::getRecentlyPlayed(int limit) {
    std::vector<AmazonMusicTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/me/recently-played?maxResults=" + std::to_string(limit));
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("tracks") && j["tracks"].is_array()) {
            for (const auto& item : j["tracks"]) tracks.push_back(parseTrack(item));
        }
    } catch (...) {}
    return tracks;
}

bool AmazonMusicService::addTrackToLibrary(const std::string& asin) {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    json body = {{"trackAsins", {asin}}};
    makeApiPostRequest(std::string(API_BASE) + "/me/library/tracks", body.dump());
    return true;
}

std::vector<AmazonMusicPlaylist> AmazonMusicService::getRecommendedPlaylists(int limit) {
    std::vector<AmazonMusicPlaylist> playlists;
    if (!isAuthenticated()) return playlists;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/recommendations/playlists?maxResults=" + std::to_string(limit));
    if (response.empty()) return playlists;

    try {
        auto j = json::parse(response);
        if (j.contains("playlists") && j["playlists"].is_array()) {
            for (const auto& item : j["playlists"]) playlists.push_back(parsePlaylist(item));
        }
    } catch (...) {}
    return playlists;
}

std::vector<AmazonMusicTrack> AmazonMusicService::getStationTracks(const std::string& stationId, int limit) {
    std::vector<AmazonMusicTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/stations/" + stationId + "/tracks?maxResults=" + std::to_string(limit));
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("tracks") && j["tracks"].is_array()) {
            for (const auto& item : j["tracks"]) tracks.push_back(parseTrack(item));
        }
    } catch (...) {}
    return tracks;
}

AmazonMusicTrack AmazonMusicService::parseTrack(const json& j) const {
    AmazonMusicTrack t;
    t.asin = j.value("asin", j.value("id", ""));
    t.title = j.value("title", j.value("name", ""));
    t.artistName = j.value("artistName", j.value("artist", ""));
    t.albumName = j.value("albumName", j.value("album", ""));
    t.albumAsin = j.value("albumAsin", "");
    t.durationMs = j.value("durationInMs", j.value("duration", 0));
    t.trackNumber = j.value("trackNumber", j.value("trackNum", 0));
    t.discNumber = j.value("discNumber", j.value("disc", 1));
    t.artworkUrl = j.value("artworkUrl", j.value("image", ""));
    t.isExplicit = j.value("isExplicit", j.value("explicit", false));
    t.isPrime = j.value("isPrime", false);
    t.isUnlimited = j.value("isUnlimited", false);
    t.tier = j.value("tier", j.value("entitlementStatus", ""));
    return t;
}

AmazonMusicAlbum AmazonMusicService::parseAlbum(const json& j) const {
    AmazonMusicAlbum a;
    a.asin = j.value("asin", j.value("id", ""));
    a.title = j.value("title", j.value("name", ""));
    a.artistName = j.value("artistName", j.value("artist", ""));
    a.artworkUrl = j.value("artworkUrl", j.value("image", ""));
    a.releaseDate = j.value("releaseDate", "");
    a.trackCount = j.value("trackCount", j.value("numTracks", 0));
    a.genre = j.value("genre", j.value("primaryGenre", ""));
    return a;
}

AmazonMusicPlaylist AmazonMusicService::parsePlaylist(const json& j) const {
    AmazonMusicPlaylist p;
    p.asin = j.value("asin", j.value("id", ""));
    p.title = j.value("title", j.value("name", ""));
    p.description = j.value("description", "");
    p.artworkUrl = j.value("artworkUrl", j.value("image", ""));
    p.trackCount = j.value("trackCount", j.value("numTracks", 0));
    p.curatorName = j.value("curatorName", j.value("owner", ""));
    p.visibility = j.value("visibility", "PUBLIC");
    return p;
}

std::string AmazonMusicService::makeApiRequest(const std::string& endpoint, const std::string& body) {
    juce::String headers = "Authorization: " + juce::String(getAuthHeader()) +
                           "\r\nx-api-key: " + juce::String(clientId_);

    HttpResponse resp;
    if (!body.empty()) {
        headers += "\r\nContent-Type: application/json";
        resp = httpSend(endpoint, "POST", body, headers);
    } else {
        resp = httpGet(endpoint, headers);
    }
    if (!resp.ok()) {
        spdlog::error("AmazonMusicService: API error: status={} for {}", resp.status, endpoint);
        return {};
    }
    return resp.body;
}

std::string AmazonMusicService::makeApiPostRequest(const std::string& endpoint, const std::string& body) {
    return makeApiRequest(endpoint, body);
}

} // namespace BeatMate::Services::Streaming
