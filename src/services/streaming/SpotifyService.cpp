#include "SpotifyService.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <random>
#include <sstream>

using json = nlohmann::json;

namespace BeatMate::Services::Streaming {


SpotifyService::SpotifyService()
    : StreamingServiceBase("Spotify", Models::StreamingServiceType::Spotify) {
    setRateLimit(30); // Spotify allows ~30 req/s
}


bool SpotifyService::authenticate(const std::string& clientId, const std::string& redirectUri,
                                   const std::vector<std::string>& scopes) {
    clientId_ = clientId;
    redirectUri_ = redirectUri;
    codeVerifier_ = generateCodeVerifier();
    stateVerifier_ = generateRandomState();

    if (loadTokenFromCache()) {
        if (!getToken().isExpired()) {
            spdlog::info("SpotifyService: Loaded valid cached token");
            return true;
        }
        if (refreshAccessToken()) {
            spdlog::info("SpotifyService: Refreshed cached token");
            return true;
        }
    }

    std::string authUrl = getAuthorizationUrl(clientId, redirectUri, scopes);
    spdlog::info("SpotifyService: Auth URL generated: {}", authUrl);

    return true;
}

bool SpotifyService::refreshAccessToken() {
    auto currentToken = getToken();
    if (currentToken.refreshToken.empty()) {
        spdlog::error("SpotifyService: No refresh token available");
        return false;
    }

    juce::String postData = "grant_type=refresh_token"
        "&refresh_token=" + juce::URL::addEscapeChars(currentToken.refreshToken, true) +
        "&client_id=" + juce::URL::addEscapeChars(clientId_, true);

    auto httpResp = httpSend(TOKEN_URL, "POST", postData.toStdString(),
                             "Content-Type: application/x-www-form-urlencoded");
    if (!httpResp.ok()) {
        spdlog::error("SpotifyService: Token refresh failed (status={})", httpResp.status);
        return false;
    }

    try {
        auto response = json::parse(httpResp.body);
        if (response.contains("error")) {
            spdlog::error("SpotifyService: Token refresh error: {}", response.value("error_description", "unknown"));
            return false;
        }

        OAuthToken token;
        token.accessToken = response["access_token"].get<std::string>();
        token.tokenType = response.value("token_type", "Bearer");
        int expiresIn = response.value("expires_in", 3600);
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        token.expiresAt = now + expiresIn;
        token.refreshToken = response.value("refresh_token", currentToken.refreshToken);
        token.scope = response.value("scope", currentToken.scope);
        setToken(token);
        saveTokenToCache();
        spdlog::info("SpotifyService: Token refreshed successfully (expires in {}s)", expiresIn);
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: Token parse error: {}", e.what());
        return false;
    }

    return true;
}

std::string SpotifyService::getAuthorizationUrl(const std::string& clientId, const std::string& redirectUri,
                                                  const std::vector<std::string>& scopes) {
    std::string codeChallenge = generateCodeChallenge(codeVerifier_);

    std::string scopeStr;
    for (size_t i = 0; i < scopes.size(); ++i) {
        if (i > 0) scopeStr += " ";
        scopeStr += scopes[i];
    }

    juce::URL url(AUTH_URL);
    url = url.withParameter("client_id", clientId)
             .withParameter("response_type", "code")
             .withParameter("redirect_uri", redirectUri)
             .withParameter("scope", scopeStr)
             .withParameter("code_challenge_method", "S256")
             .withParameter("code_challenge", codeChallenge)
             .withParameter("state", stateVerifier_)
             .withParameter("show_dialog", "false");

    return url.toString(true).toStdString();
}

bool SpotifyService::exchangeCode(const std::string& code, const std::string& codeVerifier) {
    juce::String postData = "grant_type=authorization_code"
        "&code=" + juce::URL::addEscapeChars(code, true) +
        "&redirect_uri=" + juce::URL::addEscapeChars(redirectUri_, true) +
        "&client_id=" + juce::URL::addEscapeChars(clientId_, true) +
        "&code_verifier=" + juce::URL::addEscapeChars(codeVerifier, true);

    auto httpResp = httpSend(TOKEN_URL, "POST", postData.toStdString(),
                             "Content-Type: application/x-www-form-urlencoded");
    if (!httpResp.ok()) {
        spdlog::error("SpotifyService: Code exchange failed (status={})", httpResp.status);
        return false;
    }

    try {
        auto response = json::parse(httpResp.body);
        if (response.contains("error")) {
            spdlog::error("SpotifyService: Code exchange error: {}", response.value("error_description", "unknown"));
            return false;
        }

        OAuthToken token;
        token.accessToken = response["access_token"].get<std::string>();
        token.refreshToken = response["refresh_token"].get<std::string>();
        token.tokenType = response.value("token_type", "Bearer");
        int expiresIn = response.value("expires_in", 3600);
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        token.expiresAt = now + expiresIn;
        token.scope = response.value("scope", "");
        setToken(token);
        saveTokenToCache();
        spdlog::info("SpotifyService: Authenticated successfully");
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: Token parse error: {}", e.what());
        return false;
    }

    return true;
}

bool SpotifyService::validateState(const std::string& receivedState) const {
    return !stateVerifier_.empty() && stateVerifier_ == receivedState;
}


StreamingSearchResult SpotifyService::search(const std::string& query, int limit, int offset) {
    return searchWithFilters(query, {SpotifySearchType::Track}, "", limit, offset);
}

StreamingSearchResult SpotifyService::searchWithFilters(const std::string& query,
                                                         const std::vector<SpotifySearchType>& types,
                                                         const std::string& market,
                                                         int limit, int offset) {
    StreamingSearchResult result;
    if (!isAuthenticated()) return result;
    canMakeRequest();

    std::string typeStr;
    for (size_t i = 0; i < types.size(); ++i) {
        if (i > 0) typeStr += ",";
        switch (types[i]) {
            case SpotifySearchType::Track:    typeStr += "track"; break;
            case SpotifySearchType::Album:    typeStr += "album"; break;
            case SpotifySearchType::Artist:   typeStr += "artist"; break;
            case SpotifySearchType::Playlist: typeStr += "playlist"; break;
            case SpotifySearchType::Show:     typeStr += "show"; break;
            case SpotifySearchType::Episode:  typeStr += "episode"; break;
        }
    }

    std::string endpoint = std::string(API_BASE) + "/search?q=" +
        juce::URL::addEscapeChars(query, true).toStdString() +
        "&type=" + typeStr +
        "&limit=" + std::to_string(limit) +
        "&offset=" + std::to_string(offset);
    if (!market.empty()) endpoint += "&market=" + market;

    std::string response = makeApiRequest(endpoint);
    if (response.empty()) return result;

    try {
        auto j = json::parse(response);
        if (j.contains("tracks") && j["tracks"].contains("items")) {
            for (const auto& item : j["tracks"]["items"]) {
                Models::StreamingTrack track;
                track.serviceType = Models::StreamingServiceType::Spotify;
                track.serviceId = item.value("id", "");
                track.externalId = track.serviceId;
                track.durationMs = item.value("duration_ms", 0);
                track.popularity = item.value("popularity", 0);
                track.isExplicit = item.value("explicit", false);
                track.trackNumber = item.value("track_number", 1);
                track.discNumber = item.value("disc_number", 1);

                if (item.contains("preview_url") && !item["preview_url"].is_null()) {
                    track.previewUrl = item["preview_url"].get<std::string>();
                }
                if (item.contains("external_urls") && item["external_urls"].contains("spotify")) {
                    track.externalUrl = item["external_urls"]["spotify"].get<std::string>();
                }
                if (item.contains("external_ids") && item["external_ids"].contains("isrc")) {
                    track.isrc = item["external_ids"]["isrc"].get<std::string>();
                }
                if (item.contains("album") && item["album"].contains("images") &&
                    !item["album"]["images"].empty()) {
                    track.artworkUrl = item["album"]["images"][0].value("url", "");
                }
                track.source = Models::TrackSource::Streaming;
                result.tracks.push_back(track);
            }
            result.totalResults = j["tracks"].value("total", 0);
        }
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: Search parse error: {}", e.what());
    }

    result.offset = offset;
    result.limit = limit;
    return result;
}


std::optional<Models::StreamingTrack> SpotifyService::getTrack(const std::string& trackId) {
    if (!isAuthenticated()) return std::nullopt;
    canMakeRequest();

    std::string response = makeApiRequest(std::string(API_BASE) + "/tracks/" + trackId);
    if (response.empty()) return std::nullopt;

    try {
        auto j = json::parse(response);
        Models::StreamingTrack track;
        track.serviceType = Models::StreamingServiceType::Spotify;
        track.serviceId = j.value("id", "");
        track.externalId = track.serviceId;
        track.durationMs = j.value("duration_ms", 0);
        track.popularity = j.value("popularity", 0);
        track.isExplicit = j.value("explicit", false);
        track.trackNumber = j.value("track_number", 1);
        track.discNumber = j.value("disc_number", 1);

        if (j.contains("preview_url") && !j["preview_url"].is_null())
            track.previewUrl = j["preview_url"].get<std::string>();
        if (j.contains("external_urls") && j["external_urls"].contains("spotify"))
            track.externalUrl = j["external_urls"]["spotify"].get<std::string>();
        if (j.contains("external_ids") && j["external_ids"].contains("isrc"))
            track.isrc = j["external_ids"]["isrc"].get<std::string>();
        if (j.contains("album") && j["album"].contains("images") && !j["album"]["images"].empty())
            track.artworkUrl = j["album"]["images"][0].value("url", "");

        track.source = Models::TrackSource::Streaming;
        return track;
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: getTrack parse error: {}", e.what());
        return std::nullopt;
    }
}

Models::SpotifyTrack SpotifyService::getSpotifyTrack(const std::string& trackId) {
    Models::SpotifyTrack result;
    if (!isAuthenticated()) return result;
    canMakeRequest();

    std::string response = makeApiRequest(std::string(API_BASE) + "/tracks/" + trackId);
    if (response.empty()) return result;

    try {
        result = json::parse(response).get<Models::SpotifyTrack>();
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: getSpotifyTrack parse error: {}", e.what());
    }
    return result;
}

std::vector<Models::SpotifyTrack> SpotifyService::getMultipleTracks(const std::vector<std::string>& trackIds) {
    std::vector<Models::SpotifyTrack> tracks;
    if (!isAuthenticated() || trackIds.empty()) return tracks;

    for (size_t i = 0; i < trackIds.size(); i += 50) {
        canMakeRequest();
        std::string ids;
        size_t end = std::min(i + 50, trackIds.size());
        for (size_t j = i; j < end; ++j) {
            if (j > i) ids += ",";
            ids += trackIds[j];
        }

        std::string response = makeApiRequest(std::string(API_BASE) + "/tracks?ids=" + ids);
        if (response.empty()) continue;

        try {
            auto j = json::parse(response);
            if (j.contains("tracks")) {
                for (const auto& item : j["tracks"]) {
                    if (!item.is_null()) {
                        tracks.push_back(item.get<Models::SpotifyTrack>());
                    }
                }
            }
        } catch (const std::exception& e) {
            spdlog::error("SpotifyService: getMultipleTracks parse error: {}", e.what());
        }
    }
    return tracks;
}

bool SpotifyService::isTrackSaved(const std::string& trackId) {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    std::string response = makeApiRequest(std::string(API_BASE) + "/me/tracks/contains?ids=" + trackId);
    if (response.empty()) return false;
    try {
        auto j = json::parse(response);
        return j.is_array() && !j.empty() && j[0].get<bool>();
    } catch (...) { return false; }
}

bool SpotifyService::saveTrack(const std::string& trackId) {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    json body = {{"ids", {trackId}}};
    std::string response = makeApiPutRequest(std::string(API_BASE) + "/me/tracks", body.dump());
    spdlog::info("SpotifyService: Track {} saved", trackId);
    return true;
}

bool SpotifyService::unsaveTrack(const std::string& trackId) {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    json body = {{"ids", {trackId}}};
    std::string response = makeApiDeleteRequest(std::string(API_BASE) + "/me/tracks", body.dump());
    spdlog::info("SpotifyService: Track {} unsaved", trackId);
    return true;
}


Models::SpotifyAudioFeatures SpotifyService::getAudioFeatures(const std::string& trackId) {
    Models::SpotifyAudioFeatures features;
    if (!isAuthenticated()) return features;
    canMakeRequest();

    std::string response = makeApiRequest(std::string(API_BASE) + "/audio-features/" + trackId);
    if (response.empty()) return features;

    try {
        features = json::parse(response).get<Models::SpotifyAudioFeatures>();
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: Audio features parse error: {}", e.what());
    }
    return features;
}

std::vector<Models::SpotifyAudioFeatures> SpotifyService::getMultipleAudioFeatures(const std::vector<std::string>& trackIds) {
    std::vector<Models::SpotifyAudioFeatures> allFeatures;
    if (!isAuthenticated() || trackIds.empty()) return allFeatures;

    for (size_t i = 0; i < trackIds.size(); i += 100) {
        canMakeRequest();
        std::string ids;
        size_t end = std::min(i + 100, trackIds.size());
        for (size_t j = i; j < end; ++j) {
            if (j > i) ids += ",";
            ids += trackIds[j];
        }

        std::string response = makeApiRequest(std::string(API_BASE) + "/audio-features?ids=" + ids);
        if (response.empty()) continue;

        try {
            auto j = json::parse(response);
            if (j.contains("audio_features")) {
                for (const auto& item : j["audio_features"]) {
                    if (!item.is_null()) {
                        allFeatures.push_back(item.get<Models::SpotifyAudioFeatures>());
                    }
                }
            }
        } catch (const std::exception& e) {
            spdlog::error("SpotifyService: getMultipleAudioFeatures parse error: {}", e.what());
        }
    }
    return allFeatures;
}


std::vector<Models::Playlist> SpotifyService::getPlaylists() {
    std::vector<Models::Playlist> playlists;
    if (!isAuthenticated()) return playlists;

    auto spotifyPlaylists = getSpotifyPlaylists();
    for (const auto& sp : spotifyPlaylists) {
        Models::Playlist pl;
        pl.name = sp.name;
        pl.description = sp.description;
        playlists.push_back(pl);
    }
    return playlists;
}

std::vector<Models::SpotifyPlaylist> SpotifyService::getSpotifyPlaylists(int limit, int offset) {
    std::vector<Models::SpotifyPlaylist> playlists;
    if (!isAuthenticated()) return playlists;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/me/playlists?limit=" + std::to_string(limit) +
        "&offset=" + std::to_string(offset));
    if (response.empty()) return playlists;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) {
                Models::SpotifyPlaylist pl;
                pl.id = item.value("id", "");
                pl.name = item.value("name", "");
                pl.description = item.value("description", "");
                pl.uri = item.value("uri", "");
                pl.href = item.value("href", "");
                pl.snapshotId = item.value("snapshot_id", "");
                pl.isPublic = item.value("public", false);
                pl.isCollaborative = item.value("collaborative", false);
                pl.totalTracks = item.contains("tracks") ? item["tracks"].value("total", 0) : 0;

                if (item.contains("images")) {
                    for (const auto& img : item["images"]) {
                        Models::SpotifyImage si;
                        si.url = img.value("url", "");
                        si.width = img.value("width", 0);
                        si.height = img.value("height", 0);
                        pl.images.push_back(si);
                    }
                }
                if (item.contains("owner")) {
                    pl.ownerId = item["owner"].value("id", "");
                    pl.ownerDisplayName = item["owner"].value("display_name", "");
                }
                if (item.contains("external_urls") && item["external_urls"].contains("spotify")) {
                    pl.externalUrl = item["external_urls"]["spotify"].get<std::string>();
                }
                playlists.push_back(pl);
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: getSpotifyPlaylists parse error: {}", e.what());
    }

    spdlog::info("SpotifyService: Fetched {} playlists", playlists.size());
    return playlists;
}

Models::SpotifyPlaylist SpotifyService::getPlaylistDetails(const std::string& playlistId) {
    Models::SpotifyPlaylist pl;
    if (!isAuthenticated()) return pl;
    canMakeRequest();

    std::string response = makeApiRequest(std::string(API_BASE) + "/playlists/" + playlistId);
    if (response.empty()) return pl;

    try {
        pl = json::parse(response).get<Models::SpotifyPlaylist>();
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: getPlaylistDetails parse error: {}", e.what());
    }
    return pl;
}

std::vector<Models::SpotifyTrack> SpotifyService::getPlaylistTracks(const std::string& playlistId,
                                                                      int limit, int offset) {
    std::vector<Models::SpotifyTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/playlists/" + playlistId + "/tracks?limit=" +
        std::to_string(limit) + "&offset=" + std::to_string(offset));
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) {
                if (item.contains("track") && !item["track"].is_null()) {
                    tracks.push_back(item["track"].get<Models::SpotifyTrack>());
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: getPlaylistTracks parse error: {}", e.what());
    }
    return tracks;
}

std::string SpotifyService::createPlaylist(const std::string& name, const std::string& description,
                                             bool isPublic) {
    if (!isAuthenticated()) return "";
    canMakeRequest();

    auto profile = getCurrentUserProfile();
    if (profile.id.empty()) return "";

    json body = {
        {"name", name},
        {"description", description},
        {"public", isPublic}
    };

    std::string response = makeApiPostRequest(
        std::string(API_BASE) + "/users/" + profile.id + "/playlists", body.dump());
    if (response.empty()) return "";

    try {
        auto j = json::parse(response);
        std::string playlistId = j.value("id", "");
        spdlog::info("SpotifyService: Created playlist '{}' ({})", name, playlistId);
        return playlistId;
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: createPlaylist parse error: {}", e.what());
        return "";
    }
}

bool SpotifyService::addTracksToPlaylist(const std::string& playlistId,
                                           const std::vector<std::string>& trackUris) {
    if (!isAuthenticated() || trackUris.empty()) return false;

    for (size_t i = 0; i < trackUris.size(); i += 100) {
        canMakeRequest();
        std::vector<std::string> batch(trackUris.begin() + i,
                                        trackUris.begin() + std::min(i + 100, trackUris.size()));
        json body = {{"uris", batch}};
        makeApiPostRequest(std::string(API_BASE) + "/playlists/" + playlistId + "/tracks", body.dump());
    }
    spdlog::info("SpotifyService: Added {} tracks to playlist {}", trackUris.size(), playlistId);
    return true;
}

bool SpotifyService::removeTracksFromPlaylist(const std::string& playlistId,
                                                const std::vector<std::string>& trackUris) {
    if (!isAuthenticated() || trackUris.empty()) return false;
    canMakeRequest();

    json tracks = json::array();
    for (const auto& uri : trackUris) {
        tracks.push_back({{"uri", uri}});
    }
    json body = {{"tracks", tracks}};
    makeApiDeleteRequest(std::string(API_BASE) + "/playlists/" + playlistId + "/tracks", body.dump());
    spdlog::info("SpotifyService: Removed {} tracks from playlist {}", trackUris.size(), playlistId);
    return true;
}

bool SpotifyService::reorderPlaylistTracks(const std::string& playlistId, int rangeStart,
                                             int insertBefore, int rangeLength) {
    if (!isAuthenticated()) return false;
    canMakeRequest();

    json body = {
        {"range_start", rangeStart},
        {"insert_before", insertBefore},
        {"range_length", rangeLength}
    };
    makeApiPutRequest(std::string(API_BASE) + "/playlists/" + playlistId + "/tracks", body.dump());
    return true;
}


Models::SpotifyAlbum SpotifyService::getAlbum(const std::string& albumId) {
    Models::SpotifyAlbum album;
    if (!isAuthenticated()) return album;
    canMakeRequest();

    std::string response = makeApiRequest(std::string(API_BASE) + "/albums/" + albumId);
    if (response.empty()) return album;

    try {
        album = json::parse(response).get<Models::SpotifyAlbum>();
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: getAlbum parse error: {}", e.what());
    }
    return album;
}

std::vector<Models::SpotifyTrack> SpotifyService::getAlbumTracks(const std::string& albumId) {
    std::vector<Models::SpotifyTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(std::string(API_BASE) + "/albums/" + albumId + "/tracks?limit=50");
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) {
                tracks.push_back(item.get<Models::SpotifyTrack>());
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: getAlbumTracks parse error: {}", e.what());
    }
    return tracks;
}

std::vector<Models::SpotifyAlbum> SpotifyService::getNewReleases(const std::string& country, int limit) {
    std::vector<Models::SpotifyAlbum> albums;
    if (!isAuthenticated()) return albums;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/browse/new-releases?country=" + country +
        "&limit=" + std::to_string(limit));
    if (response.empty()) return albums;

    try {
        auto j = json::parse(response);
        if (j.contains("albums") && j["albums"].contains("items")) {
            for (const auto& item : j["albums"]["items"]) {
                albums.push_back(item.get<Models::SpotifyAlbum>());
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: getNewReleases parse error: {}", e.what());
    }
    return albums;
}


Models::SpotifyArtist SpotifyService::getArtist(const std::string& artistId) {
    Models::SpotifyArtist artist;
    if (!isAuthenticated()) return artist;
    canMakeRequest();

    std::string response = makeApiRequest(std::string(API_BASE) + "/artists/" + artistId);
    if (response.empty()) return artist;

    try {
        artist = json::parse(response).get<Models::SpotifyArtist>();
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: getArtist parse error: {}", e.what());
    }
    return artist;
}

std::vector<Models::SpotifyTrack> SpotifyService::getArtistTopTracks(const std::string& artistId,
                                                                       const std::string& market) {
    std::vector<Models::SpotifyTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/artists/" + artistId + "/top-tracks?market=" + market);
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("tracks")) {
            for (const auto& item : j["tracks"]) {
                tracks.push_back(item.get<Models::SpotifyTrack>());
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: getArtistTopTracks parse error: {}", e.what());
    }
    return tracks;
}

std::vector<Models::SpotifyArtist> SpotifyService::getRelatedArtists(const std::string& artistId) {
    std::vector<Models::SpotifyArtist> artists;
    if (!isAuthenticated()) return artists;
    canMakeRequest();

    std::string response = makeApiRequest(std::string(API_BASE) + "/artists/" + artistId + "/related-artists");
    if (response.empty()) return artists;

    try {
        auto j = json::parse(response);
        if (j.contains("artists")) {
            for (const auto& item : j["artists"]) {
                artists.push_back(item.get<Models::SpotifyArtist>());
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: getRelatedArtists parse error: {}", e.what());
    }
    return artists;
}

std::vector<Models::SpotifyAlbum> SpotifyService::getArtistAlbums(const std::string& artistId, int limit) {
    std::vector<Models::SpotifyAlbum> albums;
    if (!isAuthenticated()) return albums;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/artists/" + artistId + "/albums?include_groups=album,single&limit=" +
        std::to_string(limit));
    if (response.empty()) return albums;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) {
                albums.push_back(item.get<Models::SpotifyAlbum>());
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: getArtistAlbums parse error: {}", e.what());
    }
    return albums;
}


std::vector<Models::SpotifyTrack> SpotifyService::getSavedTracks(int limit, int offset) {
    std::vector<Models::SpotifyTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/me/tracks?limit=" + std::to_string(limit) +
        "&offset=" + std::to_string(offset));
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) {
                if (item.contains("track") && !item["track"].is_null()) {
                    tracks.push_back(item["track"].get<Models::SpotifyTrack>());
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: getSavedTracks parse error: {}", e.what());
    }
    return tracks;
}

std::vector<Models::SpotifyAlbum> SpotifyService::getSavedAlbums(int limit, int offset) {
    std::vector<Models::SpotifyAlbum> albums;
    if (!isAuthenticated()) return albums;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/me/albums?limit=" + std::to_string(limit) +
        "&offset=" + std::to_string(offset));
    if (response.empty()) return albums;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) {
                if (item.contains("album")) {
                    albums.push_back(item["album"].get<Models::SpotifyAlbum>());
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: getSavedAlbums parse error: {}", e.what());
    }
    return albums;
}

std::vector<Models::SpotifyTrack> SpotifyService::getRecentlyPlayed(int limit) {
    std::vector<Models::SpotifyTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/me/player/recently-played?limit=" + std::to_string(limit));
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) {
                if (item.contains("track") && !item["track"].is_null()) {
                    tracks.push_back(item["track"].get<Models::SpotifyTrack>());
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: getRecentlyPlayed parse error: {}", e.what());
    }
    return tracks;
}

std::vector<Models::SpotifyTrack> SpotifyService::getTopTracks(const std::string& timeRange, int limit) {
    std::vector<Models::SpotifyTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/me/top/tracks?time_range=" + timeRange +
        "&limit=" + std::to_string(limit));
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) {
                tracks.push_back(item.get<Models::SpotifyTrack>());
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: getTopTracks parse error: {}", e.what());
    }
    return tracks;
}

std::vector<Models::SpotifyArtist> SpotifyService::getTopArtists(const std::string& timeRange, int limit) {
    std::vector<Models::SpotifyArtist> artists;
    if (!isAuthenticated()) return artists;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/me/top/artists?time_range=" + timeRange +
        "&limit=" + std::to_string(limit));
    if (response.empty()) return artists;

    try {
        auto j = json::parse(response);
        if (j.contains("items")) {
            for (const auto& item : j["items"]) {
                artists.push_back(item.get<Models::SpotifyArtist>());
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: getTopArtists parse error: {}", e.what());
    }
    return artists;
}


std::vector<Models::SpotifyTrack> SpotifyService::getRecommendations(const SpotifyRecommendationParams& params) {
    std::vector<Models::SpotifyTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string endpoint = std::string(API_BASE) + "/recommendations?limit=" + std::to_string(params.limit);

    if (!params.seedArtists.empty()) {
        endpoint += "&seed_artists=";
        for (size_t i = 0; i < params.seedArtists.size(); ++i) {
            if (i > 0) endpoint += ",";
            endpoint += params.seedArtists[i];
        }
    }
    if (!params.seedTracks.empty()) {
        endpoint += "&seed_tracks=";
        for (size_t i = 0; i < params.seedTracks.size(); ++i) {
            if (i > 0) endpoint += ",";
            endpoint += params.seedTracks[i];
        }
    }
    if (!params.seedGenres.empty()) {
        endpoint += "&seed_genres=";
        for (size_t i = 0; i < params.seedGenres.size(); ++i) {
            if (i > 0) endpoint += ",";
            endpoint += params.seedGenres[i];
        }
    }
    if (params.minEnergy >= 0) endpoint += "&min_energy=" + std::to_string(params.minEnergy);
    if (params.maxEnergy >= 0) endpoint += "&max_energy=" + std::to_string(params.maxEnergy);
    if (params.targetEnergy >= 0) endpoint += "&target_energy=" + std::to_string(params.targetEnergy);
    if (params.minDanceability >= 0) endpoint += "&min_danceability=" + std::to_string(params.minDanceability);
    if (params.maxDanceability >= 0) endpoint += "&max_danceability=" + std::to_string(params.maxDanceability);
    if (params.targetDanceability >= 0) endpoint += "&target_danceability=" + std::to_string(params.targetDanceability);
    if (params.minTempo >= 0) endpoint += "&min_tempo=" + std::to_string(params.minTempo);
    if (params.maxTempo >= 0) endpoint += "&max_tempo=" + std::to_string(params.maxTempo);
    if (params.targetTempo >= 0) endpoint += "&target_tempo=" + std::to_string(params.targetTempo);
    if (params.minValence >= 0) endpoint += "&min_valence=" + std::to_string(params.minValence);
    if (params.maxValence >= 0) endpoint += "&max_valence=" + std::to_string(params.maxValence);

    std::string response = makeApiRequest(endpoint);
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("tracks")) {
            for (const auto& item : j["tracks"]) {
                tracks.push_back(item.get<Models::SpotifyTrack>());
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: getRecommendations parse error: {}", e.what());
    }

    spdlog::info("SpotifyService: Got {} recommendations", tracks.size());
    return tracks;
}

std::vector<std::string> SpotifyService::getAvailableGenreSeeds() {
    std::vector<std::string> genres;
    if (!isAuthenticated()) return genres;
    canMakeRequest();

    std::string response = makeApiRequest(std::string(API_BASE) + "/recommendations/available-genre-seeds");
    if (response.empty()) return genres;

    try {
        auto j = json::parse(response);
        if (j.contains("genres")) {
            genres = j["genres"].get<std::vector<std::string>>();
        }
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: getAvailableGenreSeeds parse error: {}", e.what());
    }
    return genres;
}


SpotifyUserProfile SpotifyService::getCurrentUserProfile() {
    SpotifyUserProfile profile;
    if (!isAuthenticated()) return profile;
    canMakeRequest();

    std::string response = makeApiRequest(std::string(API_BASE) + "/me");
    if (response.empty()) return profile;

    try {
        auto j = json::parse(response);
        profile.id = j.value("id", "");
        profile.displayName = j.value("display_name", "");
        profile.email = j.value("email", "");
        profile.country = j.value("country", "");
        profile.product = j.value("product", "free");
        if (j.contains("followers")) profile.followers = j["followers"].value("total", 0);
        if (j.contains("images") && !j["images"].empty()) {
            profile.profileImageUrl = j["images"][0].value("url", "");
        }
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: getCurrentUserProfile parse error: {}", e.what());
    }
    return profile;
}

SpotifyUserProfile SpotifyService::getUserProfile(const std::string& userId) {
    SpotifyUserProfile profile;
    if (!isAuthenticated()) return profile;
    canMakeRequest();

    std::string response = makeApiRequest(std::string(API_BASE) + "/users/" + userId);
    if (response.empty()) return profile;

    try {
        auto j = json::parse(response);
        profile.id = j.value("id", "");
        profile.displayName = j.value("display_name", "");
        if (j.contains("followers")) profile.followers = j["followers"].value("total", 0);
        if (j.contains("images") && !j["images"].empty()) {
            profile.profileImageUrl = j["images"][0].value("url", "");
        }
    } catch (const std::exception& e) {
        spdlog::error("SpotifyService: getUserProfile parse error: {}", e.what());
    }
    return profile;
}


bool SpotifyService::startPlayback(const std::string& contextUri, const std::vector<std::string>& uris,
                                     int offsetPosition) {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    json body = json::object();
    if (!contextUri.empty()) body["context_uri"] = contextUri;
    if (!uris.empty()) body["uris"] = uris;
    if (offsetPosition > 0) body["offset"] = {{"position", offsetPosition}};
    makeApiPutRequest(std::string(API_BASE) + "/me/player/play", body.dump());
    return true;
}

bool SpotifyService::pausePlayback() {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    makeApiPutRequest(std::string(API_BASE) + "/me/player/pause");
    return true;
}

bool SpotifyService::skipToNext() {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    makeApiPostRequest(std::string(API_BASE) + "/me/player/next", "");
    return true;
}

bool SpotifyService::skipToPrevious() {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    makeApiPostRequest(std::string(API_BASE) + "/me/player/previous", "");
    return true;
}

bool SpotifyService::seekToPosition(int positionMs) {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    makeApiPutRequest(std::string(API_BASE) + "/me/player/seek?position_ms=" + std::to_string(positionMs));
    return true;
}

bool SpotifyService::setVolume(int volumePercent) {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    makeApiPutRequest(std::string(API_BASE) + "/me/player/volume?volume_percent=" + std::to_string(volumePercent));
    return true;
}

bool SpotifyService::setRepeatMode(const std::string& state) {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    makeApiPutRequest(std::string(API_BASE) + "/me/player/repeat?state=" + state);
    return true;
}

bool SpotifyService::toggleShuffle(bool state) {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    makeApiPutRequest(std::string(API_BASE) + "/me/player/shuffle?state=" + std::string(state ? "true" : "false"));
    return true;
}

bool SpotifyService::addToQueue(const std::string& uri) {
    if (!isAuthenticated()) return false;
    canMakeRequest();
    makeApiPostRequest(std::string(API_BASE) + "/me/player/queue?uri=" +
                        juce::URL::addEscapeChars(uri, true).toStdString(), "");
    return true;
}


std::string SpotifyService::makeApiRequest(const std::string& endpoint) {
    auto ensureValidToken = [this]() {
        if (getToken().isExpired()) {
            spdlog::debug("SpotifyService: Token expired, refreshing...");
            refreshAccessToken();
        }
    };
    ensureValidToken();

    auto resp = httpGet(endpoint, "Authorization: " + juce::String(getAuthHeader()));
    if (resp.status == 401 || (!resp.transportOk)) {
        spdlog::warn("SpotifyService: API error (status={}), retrying after token refresh for: {}",
                     resp.status, endpoint);
        if (refreshAccessToken()) {
            resp = httpGet(endpoint, "Authorization: " + juce::String(getAuthHeader()));
        }
    }
    if (!resp.ok()) {
        spdlog::error("SpotifyService: API request failed (status={}): {}", resp.status, endpoint);
        return {};
    }
    return resp.body;
}

std::string SpotifyService::makeApiPostRequest(const std::string& endpoint, const std::string& body) {
    juce::String headers = "Authorization: " + juce::String(getAuthHeader()) +
                           "\r\nContent-Type: application/json";

    auto resp = httpSend(endpoint, "POST", body, headers);
    if (!resp.ok()) {
        spdlog::error("SpotifyService: POST request failed (status={}): {}", resp.status, endpoint);
        return {};
    }
    return resp.body;
}

std::string SpotifyService::makeApiPutRequest(const std::string& endpoint, const std::string& body) {
    juce::String headers = "Authorization: " + juce::String(getAuthHeader()) +
                           "\r\nContent-Type: application/json";

    auto resp = httpSend(endpoint, "PUT", body, headers);
    if (!resp.ok()) {
        spdlog::error("SpotifyService: PUT request failed (status={}): {}", resp.status, endpoint);
        return {};
    }
    return resp.body;
}

std::string SpotifyService::makeApiDeleteRequest(const std::string& endpoint, const std::string& body) {
    juce::String headers = "Authorization: " + juce::String(getAuthHeader()) +
                           "\r\nContent-Type: application/json";

    auto resp = httpSend(endpoint, "DELETE", body, headers);
    if (!resp.ok()) {
        spdlog::error("SpotifyService: DELETE request failed (status={}): {}", resp.status, endpoint);
        return {};
    }
    return resp.body;
}


std::string SpotifyService::generateCodeVerifier() {
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    std::string verifier;
    verifier.reserve(128);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, static_cast<int>(chars.size()) - 1);

    for (int i = 0; i < 128; ++i) {
        verifier += chars[dist(gen)];
    }
    return verifier;
}

std::string SpotifyService::generateCodeChallenge(const std::string& verifier) {
    juce::SHA256 sha256(verifier.data(), verifier.size());
    auto hashResult = sha256.getRawData();
    juce::MemoryBlock mb(hashResult.getData(), hashResult.getSize());
    juce::String b64 = mb.toBase64Encoding();
    std::string result = b64.toStdString();
    for (auto& c : result) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!result.empty() && result.back() == '=') result.pop_back();
    return result;
}

std::string SpotifyService::generateRandomState(int length) {
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::string state;
    state.reserve(length);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, static_cast<int>(chars.size()) - 1);
    for (int i = 0; i < length; ++i) {
        state += chars[dist(gen)];
    }
    return state;
}


void SpotifyService::saveTokenToCache() {
    if (tokenCachePath_.empty()) {
        auto appDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("BeatMate");
        appDir.createDirectory();
        tokenCachePath_ = appDir.getChildFile("spotify_token.json").getFullPathName().toStdString();
    }

    auto token = getToken();
    json j = {
        {"access_token", token.accessToken},
        {"refresh_token", token.refreshToken},
        {"token_type", token.tokenType},
        {"expires_at", token.expiresAt},
        {"scope", token.scope}
    };

    std::ofstream ofs(tokenCachePath_);
    if (ofs.is_open()) {
        ofs << j.dump(2);
        spdlog::debug("SpotifyService: Token cached to {}", tokenCachePath_);
    }
}

bool SpotifyService::loadTokenFromCache() {
    if (tokenCachePath_.empty()) {
        auto appDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("BeatMate");
        tokenCachePath_ = appDir.getChildFile("spotify_token.json").getFullPathName().toStdString();
    }

    std::ifstream ifs(tokenCachePath_);
    if (!ifs.is_open()) return false;

    try {
        auto j = json::parse(ifs);
        OAuthToken token;
        token.accessToken = j.value("access_token", "");
        token.refreshToken = j.value("refresh_token", "");
        token.tokenType = j.value("token_type", "Bearer");
        token.expiresAt = j.value("expires_at", static_cast<int64_t>(0));
        token.scope = j.value("scope", "");
        if (token.accessToken.empty()) return false;
        setToken(token);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace BeatMate::Services::Streaming
