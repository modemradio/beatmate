#include "TidalService.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <juce_core/juce_core.h>

using json = nlohmann::json;

namespace BeatMate::Services::Streaming {

TidalService::TidalService()
    : StreamingServiceBase("TIDAL", Models::StreamingServiceType::Tidal) {
    setRateLimit(20);
}

bool TidalService::authenticate(const std::string& clientId, const std::string& redirectUri,
                                 const std::vector<std::string>& scopes) {
    clientId_ = clientId;
    spdlog::info("TidalService: Starting OAuth2 PKCE authentication");
    return true;
}

bool TidalService::refreshAccessToken() {
    auto token = getToken();
    if (token.refreshToken.empty()) return false;
    canMakeRequest();

    juce::String postData =
        "grant_type=refresh_token"
        "&refresh_token=" + juce::URL::addEscapeChars(token.refreshToken, true) +
        "&client_id=" + juce::URL::addEscapeChars(clientId_, true);

    auto httpResp = httpSend(std::string(AUTH_BASE) + "/token", "POST",
                             postData.toStdString(),
                             "Content-Type: application/x-www-form-urlencoded");
    if (!httpResp.ok()) {
        spdlog::error("TidalService: Token refresh failed (status={})", httpResp.status);
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
        userId_ = resp.value("user", json{}).value("userId", static_cast<int64_t>(0));
        countryCode_ = resp.value("user", json{}).value("countryCode", "US");
        spdlog::info("TidalService: Token refreshed (country: {})", countryCode_);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("TidalService: Token refresh parse error: {}", e.what());
        return false;
    }
}

bool TidalService::exchangeCode(const std::string& code, const std::string& codeVerifier) {
    juce::String postData =
        "grant_type=authorization_code"
        "&client_id=" + juce::URL::addEscapeChars(clientId_, true) +
        "&code=" + juce::URL::addEscapeChars(code, true) +
        "&code_verifier=" + juce::URL::addEscapeChars(codeVerifier, true) +
        "&redirect_uri=beatmate://tidal/callback";

    auto httpResp = httpSend(std::string(AUTH_BASE) + "/token", "POST",
                             postData.toStdString(),
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
        userId_ = resp.value("user", json{}).value("userId", static_cast<int64_t>(0));
        countryCode_ = resp.value("user", json{}).value("countryCode", "US");
        spdlog::info("TidalService: Authenticated (userId: {}, country: {})", userId_, countryCode_);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("TidalService: Code exchange error: {}", e.what());
        return false;
    }
}

std::string TidalService::getAuthorizationUrl(const std::string& clientId, const std::string& redirectUri) {
    return std::string(AUTH_BASE) + "/authorize?response_type=code&client_id=" + clientId +
           "&redirect_uri=" + redirectUri + "&code_challenge_method=S256";
}

StreamingSearchResult TidalService::search(const std::string& query, int limit, int offset) {
    StreamingSearchResult result;
    auto tracks = searchTracks(query, limit, offset);
    for (const auto& tt : tracks) {
        Models::StreamingTrack t;
        t.serviceType = Models::StreamingServiceType::Tidal;
        t.serviceId = std::to_string(tt.id);
        t.durationMs = tt.durationSec * 1000;
        t.popularity = tt.popularity;
        t.isrc = tt.isrc;
        t.isExplicit = tt.isExplicit;
        t.artworkUrl = tt.artworkUrl;
        t.externalUrl = tt.url;
        t.source = Models::TrackSource::Streaming;
        result.tracks.push_back(t);
    }
    result.offset = offset;
    result.limit = limit;
    return result;
}

std::vector<TidalTrack> TidalService::searchTracks(const std::string& query, int limit, int offset) {
    std::vector<TidalTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/search/tracks?query=" +
        juce::URL::addEscapeChars(query, true).toStdString() +
        "&limit=" + std::to_string(limit) + "&offset=" + std::to_string(offset) +
        "&countryCode=" + countryCode_);

    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) {
                tracks.push_back(parseTrack(item));
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("TidalService: searchTracks parse error: {}", e.what());
    }

    spdlog::info("TidalService: Search '{}' returned {} tracks", query, tracks.size());
    return tracks;
}

std::vector<TidalAlbum> TidalService::searchAlbums(const std::string& query, int limit) {
    std::vector<TidalAlbum> albums;
    if (!isAuthenticated()) return albums;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/search/albums?query=" +
        juce::URL::addEscapeChars(query, true).toStdString() +
        "&limit=" + std::to_string(limit) + "&countryCode=" + countryCode_);
    if (response.empty()) return albums;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) albums.push_back(parseAlbum(item));
        }
    } catch (...) {}
    return albums;
}

std::vector<TidalArtist> TidalService::searchArtists(const std::string& query, int limit) {
    std::vector<TidalArtist> artists;
    if (!isAuthenticated()) return artists;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/search/artists?query=" +
        juce::URL::addEscapeChars(query, true).toStdString() +
        "&limit=" + std::to_string(limit) + "&countryCode=" + countryCode_);
    if (response.empty()) return artists;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) artists.push_back(parseArtist(item));
        }
    } catch (...) {}
    return artists;
}

std::vector<TidalPlaylist> TidalService::searchPlaylists(const std::string& query, int limit) {
    std::vector<TidalPlaylist> playlists;
    if (!isAuthenticated()) return playlists;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/search/playlists?query=" +
        juce::URL::addEscapeChars(query, true).toStdString() +
        "&limit=" + std::to_string(limit) + "&countryCode=" + countryCode_);
    if (response.empty()) return playlists;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) playlists.push_back(parsePlaylist(item));
        }
    } catch (...) {}
    return playlists;
}

std::optional<Models::StreamingTrack> TidalService::getTrack(const std::string& trackId) {
    auto tt = getTidalTrack(trackId);
    if (tt.id == 0) return std::nullopt;

    Models::StreamingTrack t;
    t.serviceType = Models::StreamingServiceType::Tidal;
    t.serviceId = std::to_string(tt.id);
    t.durationMs = tt.durationSec * 1000;
    t.popularity = tt.popularity;
    t.isrc = tt.isrc;
    t.artworkUrl = tt.artworkUrl;
    t.source = Models::TrackSource::Streaming;
    return t;
}

TidalTrack TidalService::getTidalTrack(const std::string& trackId) {
    TidalTrack track;
    if (!isAuthenticated()) return track;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/tracks/" + trackId + "?countryCode=" + countryCode_);
    if (response.empty()) return track;

    try {
        track = parseTrack(json::parse(response));
    } catch (...) {}
    return track;
}

std::string TidalService::getStreamQuality(const std::string& trackId) {
    if (!isAuthenticated()) return "STANDARD";
    canMakeRequest();

    std::string qualStr = qualityToString(preferredQuality_);
    std::string response = makeApiRequest(
        std::string(API_BASE) + "/tracks/" + trackId +
        "/playbackinfopostpaywall?audioquality=" + qualStr +
        "&playbackmode=STREAM&assetpresentation=FULL");
    if (response.empty()) return "STANDARD";

    try {
        auto j = json::parse(response);
        return j.value("audioQuality", "STANDARD");
    } catch (...) { return "STANDARD"; }
}

std::string TidalService::getStreamUrl(const std::string& trackId, TidalQuality quality) {
    if (!isAuthenticated()) return "";
    canMakeRequest();

    std::string qualStr = qualityToString(quality);
    std::string response = makeApiRequest(
        std::string(API_BASE) + "/tracks/" + trackId +
        "/urlpostpaywall?audioquality=" + qualStr + "&urlusagemode=STREAM");
    if (response.empty()) return "";

    try {
        auto j = json::parse(response);
        return j.value("url", "");
    } catch (...) { return ""; }
}

TidalAlbum TidalService::getAlbum(const std::string& albumId) {
    TidalAlbum album;
    if (!isAuthenticated()) return album;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/albums/" + albumId + "?countryCode=" + countryCode_);
    if (response.empty()) return album;
    try { album = parseAlbum(json::parse(response)); } catch (...) {}
    return album;
}

std::vector<TidalTrack> TidalService::getAlbumTracks(const std::string& albumId) {
    std::vector<TidalTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/albums/" + albumId + "/tracks?countryCode=" + countryCode_);
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) tracks.push_back(parseTrack(item));
        }
    } catch (...) {}
    return tracks;
}

TidalArtist TidalService::getArtist(const std::string& artistId) {
    TidalArtist artist;
    if (!isAuthenticated()) return artist;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/artists/" + artistId + "?countryCode=" + countryCode_);
    if (response.empty()) return artist;
    try { artist = parseArtist(json::parse(response)); } catch (...) {}
    return artist;
}

std::vector<TidalTrack> TidalService::getArtistTopTracks(const std::string& artistId, int limit) {
    std::vector<TidalTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/artists/" + artistId + "/toptracks?limit=" +
        std::to_string(limit) + "&countryCode=" + countryCode_);
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) tracks.push_back(parseTrack(item));
        }
    } catch (...) {}
    return tracks;
}

std::vector<TidalAlbum> TidalService::getArtistAlbums(const std::string& artistId, int limit) {
    std::vector<TidalAlbum> albums;
    if (!isAuthenticated()) return albums;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/artists/" + artistId + "/albums?limit=" +
        std::to_string(limit) + "&countryCode=" + countryCode_);
    if (response.empty()) return albums;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) albums.push_back(parseAlbum(item));
        }
    } catch (...) {}
    return albums;
}

std::vector<TidalArtist> TidalService::getSimilarArtists(const std::string& artistId, int limit) {
    std::vector<TidalArtist> artists;
    if (!isAuthenticated()) return artists;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/artists/" + artistId + "/similar?limit=" +
        std::to_string(limit) + "&countryCode=" + countryCode_);
    if (response.empty()) return artists;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) artists.push_back(parseArtist(item));
        }
    } catch (...) {}
    return artists;
}

std::vector<Models::Playlist> TidalService::getPlaylists() {
    std::vector<Models::Playlist> playlists;
    auto tidalPlaylists = getUserPlaylists();
    for (const auto& tp : tidalPlaylists) {
        Models::Playlist pl;
        pl.name = tp.title;
        pl.description = tp.description;
        playlists.push_back(pl);
    }
    return playlists;
}

std::vector<TidalPlaylist> TidalService::getUserPlaylists() {
    std::vector<TidalPlaylist> playlists;
    if (!isAuthenticated() || userId_ == 0) return playlists;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/users/" + std::to_string(userId_) +
        "/playlists?countryCode=" + countryCode_);
    if (response.empty()) return playlists;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) playlists.push_back(parsePlaylist(item));
        }
    } catch (...) {}
    return playlists;
}

TidalPlaylist TidalService::getPlaylistDetails(const std::string& playlistUuid) {
    TidalPlaylist pl;
    if (!isAuthenticated()) return pl;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/playlists/" + playlistUuid + "?countryCode=" + countryCode_);
    if (response.empty()) return pl;
    try { pl = parsePlaylist(json::parse(response)); } catch (...) {}
    return pl;
}

std::vector<TidalTrack> TidalService::getPlaylistTracks(const std::string& playlistUuid,
                                                          int limit, int offset) {
    std::vector<TidalTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/playlists/" + playlistUuid + "/tracks?limit=" +
        std::to_string(limit) + "&offset=" + std::to_string(offset) +
        "&countryCode=" + countryCode_);
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) tracks.push_back(parseTrack(item));
        }
    } catch (...) {}
    return tracks;
}

std::string TidalService::createPlaylist(const std::string& title, const std::string& description) {
    if (!isAuthenticated() || userId_ == 0) return "";
    canMakeRequest();

    json body = {{"title", title}, {"description", description}};
    std::string response = makeApiPostRequest(
        std::string(API_BASE) + "/users/" + std::to_string(userId_) +
        "/playlists?countryCode=" + countryCode_, body.dump());
    if (response.empty()) return "";

    try {
        auto j = json::parse(response);
        std::string uuid = j.value("uuid", "");
        spdlog::info("TidalService: Created playlist '{}' ({})", title, uuid);
        return uuid;
    } catch (...) { return ""; }
}

bool TidalService::addTracksToPlaylist(const std::string& playlistUuid,
                                        const std::vector<std::string>& trackIds) {
    if (!isAuthenticated() || trackIds.empty()) return false;
    canMakeRequest();

    std::string ids;
    for (size_t i = 0; i < trackIds.size(); ++i) {
        if (i > 0) ids += ",";
        ids += trackIds[i];
    }

    json body = {{"trackIds", ids}};
    makeApiPostRequest(
        std::string(API_BASE) + "/playlists/" + playlistUuid + "/items?countryCode=" + countryCode_,
        body.dump());
    spdlog::info("TidalService: Added {} tracks to playlist {}", trackIds.size(), playlistUuid);
    return true;
}

std::vector<TidalTrack> TidalService::getFavoriteTracks(int limit, int offset) {
    std::vector<TidalTrack> tracks;
    if (!isAuthenticated() || userId_ == 0) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/users/" + std::to_string(userId_) +
        "/favorites/tracks?limit=" + std::to_string(limit) +
        "&offset=" + std::to_string(offset) + "&countryCode=" + countryCode_);
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) {
                if (item.contains("item")) tracks.push_back(parseTrack(item["item"]));
            }
        }
    } catch (...) {}
    return tracks;
}

std::vector<TidalAlbum> TidalService::getFavoriteAlbums(int limit) {
    std::vector<TidalAlbum> albums;
    if (!isAuthenticated() || userId_ == 0) return albums;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/users/" + std::to_string(userId_) +
        "/favorites/albums?limit=" + std::to_string(limit) + "&countryCode=" + countryCode_);
    if (response.empty()) return albums;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) {
                if (item.contains("item")) albums.push_back(parseAlbum(item["item"]));
            }
        }
    } catch (...) {}
    return albums;
}

bool TidalService::addFavoriteTrack(const std::string& trackId) {
    if (!isAuthenticated() || userId_ == 0) return false;
    canMakeRequest();
    json body = {{"trackIds", trackId}};
    makeApiPostRequest(
        std::string(API_BASE) + "/users/" + std::to_string(userId_) +
        "/favorites/tracks?countryCode=" + countryCode_, body.dump());
    return true;
}

bool TidalService::removeFavoriteTrack(const std::string& trackId) {
    if (!isAuthenticated() || userId_ == 0) return false;
    canMakeRequest();
    makeApiDeleteRequest(
        std::string(API_BASE) + "/users/" + std::to_string(userId_) +
        "/favorites/tracks/" + trackId + "?countryCode=" + countryCode_);
    return true;
}

std::vector<TidalTrack> TidalService::getMixTracks(const std::string& mixId) {
    std::vector<TidalTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/mixes/" + mixId + "/items?countryCode=" + countryCode_);
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) {
                if (item.contains("item")) tracks.push_back(parseTrack(item["item"]));
            }
        }
    } catch (...) {}
    return tracks;
}

std::vector<TidalPlaylist> TidalService::getEditorialPlaylists(int limit) {
    std::vector<TidalPlaylist> playlists;
    if (!isAuthenticated()) return playlists;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/promotions?limit=" + std::to_string(limit) +
        "&countryCode=" + countryCode_);
    // Editorial endpoint varies; parse what's available
    if (response.empty()) return playlists;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) {
                if (item.value("type", "") == "PLAYLIST") {
                    playlists.push_back(parsePlaylist(item));
                }
            }
        }
    } catch (...) {}
    return playlists;
}

std::string TidalService::qualityToString(TidalQuality q) {
    switch (q) {
        case TidalQuality::Low:      return "LOW";
        case TidalQuality::High:     return "HIGH";
        case TidalQuality::Lossless: return "LOSSLESS";
        case TidalQuality::HiRes:    return "HI_RES";
        case TidalQuality::Master:   return "HI_RES_LOSSLESS";
    }
    return "LOSSLESS";
}

TidalTrack TidalService::parseTrack(const json& j) const {
    TidalTrack t;
    t.id = j.value("id", static_cast<int64_t>(0));
    t.title = j.value("title", "");
    t.isrc = j.value("isrc", "");
    t.durationSec = j.value("duration", 0);
    t.trackNumber = j.value("trackNumber", 0);
    t.volumeNumber = j.value("volumeNumber", 1);
    t.quality = j.value("audioQuality", "");
    t.popularity = j.value("popularity", 0);
    t.isExplicit = j.value("explicit", false);
    t.allowStreaming = j.value("allowStreaming", true);
    t.copyright = j.value("copyright", "");
    t.url = j.value("url", "");

    if (j.contains("artist")) {
        t.artistName = j["artist"].value("name", "");
    } else if (j.contains("artists") && !j["artists"].empty()) {
        t.artistName = j["artists"][0].value("name", "");
    }

    if (j.contains("album")) {
        t.albumName = j["album"].value("title", "");
        t.albumId = j["album"].value("id", static_cast<int64_t>(0));
        auto coverId = j["album"].value("cover", "");
        if (!coverId.empty()) {
            std::string cid = coverId;
            for (auto& c : cid) { if (c == '-') c = '/'; }
            t.artworkUrl = "https://resources.tidal.com/images/" + cid + "/640x640.jpg";
        }
    }

    t.replayGain = j.value("replayGain", 0.0f);
    t.peakAmplitude = j.value("peak", 1.0f);
    return t;
}

TidalAlbum TidalService::parseAlbum(const json& j) const {
    TidalAlbum a;
    a.id = j.value("id", static_cast<int64_t>(0));
    a.title = j.value("title", "");
    a.releaseDate = j.value("releaseDate", "");
    a.numTracks = j.value("numberOfTracks", 0);
    a.numVolumes = j.value("numberOfVolumes", 1);
    a.quality = j.value("audioQuality", "");
    a.durationSec = j.value("duration", 0);
    a.copyright = j.value("copyright", "");
    a.isExplicit = j.value("explicit", false);
    a.type = j.value("type", "ALBUM");

    if (j.contains("artist")) a.artistName = j["artist"].value("name", "");
    else if (j.contains("artists") && !j["artists"].empty())
        a.artistName = j["artists"][0].value("name", "");

    auto coverId = j.value("cover", "");
    if (!coverId.empty()) {
        std::string cid = coverId;
        for (auto& c : cid) { if (c == '-') c = '/'; }
        a.artworkUrl = "https://resources.tidal.com/images/" + cid + "/640x640.jpg";
    }
    return a;
}

TidalPlaylist TidalService::parsePlaylist(const json& j) const {
    TidalPlaylist p;
    p.uuid = j.value("uuid", "");
    p.title = j.value("title", "");
    p.description = j.value("description", "");
    p.numTracks = j.value("numberOfTracks", 0);
    p.durationSec = j.value("duration", 0);
    p.type = j.value("type", "");
    p.created = j.value("created", "");
    p.lastUpdated = j.value("lastUpdated", "");

    if (j.contains("creator")) p.creatorName = j["creator"].value("name", "");

    auto squareImgId = j.value("squareImage", j.value("image", ""));
    if (!squareImgId.empty()) {
        std::string sid = squareImgId;
        for (auto& c : sid) { if (c == '-') c = '/'; }
        p.artworkUrl = "https://resources.tidal.com/images/" + sid + "/640x640.jpg";
    }
    return p;
}

TidalArtist TidalService::parseArtist(const json& j) const {
    TidalArtist a;
    a.id = j.value("id", static_cast<int64_t>(0));
    a.name = j.value("name", "");
    a.popularity = j.value("popularity", 0);

    auto picId = j.value("picture", "");
    if (!picId.empty()) {
        std::string pid = picId;
        for (auto& c : pid) { if (c == '-') c = '/'; }
        a.artworkUrl = "https://resources.tidal.com/images/" + pid + "/320x320.jpg";
    }
    return a;
}

std::string TidalService::makeApiRequest(const std::string& endpoint) {
    auto resp = httpGet(endpoint, "Authorization: " + juce::String(getAuthHeader()));
    if (!resp.ok()) {
        spdlog::error("TidalService: API error: status={} for {}", resp.status, endpoint);
        return {};
    }
    return resp.body;
}

std::string TidalService::makeApiPostRequest(const std::string& endpoint, const std::string& body) {
    juce::String headers = "Authorization: " + juce::String(getAuthHeader()) +
                           "\r\nContent-Type: application/json";
    auto resp = httpSend(endpoint, "POST", body, headers);
    return resp.ok() ? resp.body : std::string{};
}

std::string TidalService::makeApiDeleteRequest(const std::string& endpoint) {
    auto resp = httpSend(endpoint, "DELETE", {},
                         "Authorization: " + juce::String(getAuthHeader()));
    return resp.ok() ? resp.body : std::string{};
}

} // namespace BeatMate::Services::Streaming
