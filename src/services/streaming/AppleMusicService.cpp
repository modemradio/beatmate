#include "AppleMusicService.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <regex>

using json = nlohmann::json;

namespace BeatMate::Services::Streaming {

AppleMusicService::AppleMusicService()
    : StreamingServiceBase("Apple Music", Models::StreamingServiceType::AppleMusic) {
    setRateLimit(20);
}

bool AppleMusicService::authenticate(const std::string& clientId, const std::string& redirectUri,
                                      const std::vector<std::string>& scopes) {
    developerToken_ = clientId;
    spdlog::info("AppleMusicService: Developer token set ({}... chars)", developerToken_.size());
    return !developerToken_.empty();
}

bool AppleMusicService::refreshAccessToken() {
    // Developer tokens are JWTs that don't expire quickly (typically 6 months)
    return !developerToken_.empty();
}

StreamingSearchResult AppleMusicService::search(const std::string& query, int limit, int offset) {
    StreamingSearchResult result;
    auto songs = searchSongs(query, limit, offset);
    for (const auto& song : songs) {
        Models::StreamingTrack track;
        track.serviceType = Models::StreamingServiceType::AppleMusic;
        track.serviceId = song.id;
        track.durationMs = song.durationMs;
        track.previewUrl = song.previewUrl;
        track.artworkUrl = formatArtworkUrl(song.artworkUrl, 300, 300);
        track.isrc = song.isrc;
        track.isExplicit = song.isExplicit;
        track.trackNumber = song.trackNumber;
        track.discNumber = song.discNumber;
        track.externalUrl = "https://music.apple.com/song/" + song.id;
        track.source = Models::TrackSource::Streaming;
        result.tracks.push_back(track);
    }
    result.offset = offset;
    result.limit = limit;
    return result;
}

std::vector<AppleMusicSong> AppleMusicService::searchSongs(const std::string& query, int limit, int offset) {
    std::vector<AppleMusicSong> songs;
    if (developerToken_.empty()) return songs;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/catalog/" + storefront_ + "/search?types=songs&term=" +
        juce::URL::addEscapeChars(query, true).toStdString() +
        "&limit=" + std::to_string(limit) +
        "&offset=" + std::to_string(offset));

    if (response.empty()) return songs;

    try {
        auto j = json::parse(response);
        if (j.contains("results") && j["results"].contains("songs") &&
            j["results"]["songs"].contains("data")) {
            for (const auto& item : j["results"]["songs"]["data"]) {
                songs.push_back(parseSong(item));
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("AppleMusicService: searchSongs parse error: {}", e.what());
    }

    spdlog::info("AppleMusicService: Search '{}' returned {} songs", query, songs.size());
    return songs;
}

std::vector<AppleMusicAlbum> AppleMusicService::searchAlbums(const std::string& query, int limit) {
    std::vector<AppleMusicAlbum> albums;
    if (developerToken_.empty()) return albums;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/catalog/" + storefront_ + "/search?types=albums&term=" +
        juce::URL::addEscapeChars(query, true).toStdString() +
        "&limit=" + std::to_string(limit));

    if (response.empty()) return albums;

    try {
        auto j = json::parse(response);
        if (j.contains("results") && j["results"].contains("albums") &&
            j["results"]["albums"].contains("data")) {
            for (const auto& item : j["results"]["albums"]["data"]) {
                albums.push_back(parseAlbum(item));
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("AppleMusicService: searchAlbums parse error: {}", e.what());
    }
    return albums;
}

std::optional<Models::StreamingTrack> AppleMusicService::getTrack(const std::string& trackId) {
    auto song = getSong(trackId);
    if (song.id.empty()) return std::nullopt;

    Models::StreamingTrack track;
    track.serviceType = Models::StreamingServiceType::AppleMusic;
    track.serviceId = song.id;
    track.durationMs = song.durationMs;
    track.previewUrl = song.previewUrl;
    track.artworkUrl = formatArtworkUrl(song.artworkUrl, 300, 300);
    track.isrc = song.isrc;
    track.isExplicit = song.isExplicit;
    track.trackNumber = song.trackNumber;
    track.discNumber = song.discNumber;
    track.source = Models::TrackSource::Streaming;
    return track;
}

AppleMusicSong AppleMusicService::getSong(const std::string& songId) {
    AppleMusicSong song;
    if (developerToken_.empty()) return song;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/catalog/" + storefront_ + "/songs/" + songId);
    if (response.empty()) return song;

    try {
        auto j = json::parse(response);
        if (j.contains("data") && !j["data"].empty()) {
            song = parseSong(j["data"][0]);
        }
    } catch (const std::exception& e) {
        spdlog::error("AppleMusicService: getSong parse error: {}", e.what());
    }
    return song;
}

std::vector<AppleMusicSong> AppleMusicService::getMultipleSongs(const std::vector<std::string>& songIds) {
    std::vector<AppleMusicSong> songs;
    if (developerToken_.empty() || songIds.empty()) return songs;

    for (size_t i = 0; i < songIds.size(); i += 300) {
        canMakeRequest();
        std::string ids;
        size_t end = std::min(i + 300, songIds.size());
        for (size_t j = i; j < end; ++j) {
            if (j > i) ids += ",";
            ids += songIds[j];
        }

        std::string response = makeApiRequest(
            std::string(API_BASE) + "/catalog/" + storefront_ + "/songs?ids=" + ids);
        if (response.empty()) continue;

        try {
            auto j = json::parse(response);
            if (j.contains("data")) {
                for (const auto& item : j["data"]) {
                    songs.push_back(parseSong(item));
                }
            }
        } catch (const std::exception& e) {
            spdlog::error("AppleMusicService: getMultipleSongs parse error: {}", e.what());
        }
    }
    return songs;
}

AppleMusicAlbum AppleMusicService::getAlbum(const std::string& albumId) {
    AppleMusicAlbum album;
    if (developerToken_.empty()) return album;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/catalog/" + storefront_ + "/albums/" + albumId);
    if (response.empty()) return album;

    try {
        auto j = json::parse(response);
        if (j.contains("data") && !j["data"].empty()) {
            album = parseAlbum(j["data"][0]);
        }
    } catch (const std::exception& e) {
        spdlog::error("AppleMusicService: getAlbum parse error: {}", e.what());
    }
    return album;
}

std::vector<AppleMusicSong> AppleMusicService::getAlbumTracks(const std::string& albumId) {
    std::vector<AppleMusicSong> tracks;
    if (developerToken_.empty()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/catalog/" + storefront_ + "/albums/" + albumId + "/tracks");
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("data")) {
            for (const auto& item : j["data"]) {
                tracks.push_back(parseSong(item));
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("AppleMusicService: getAlbumTracks parse error: {}", e.what());
    }
    return tracks;
}

std::vector<AppleMusicSong> AppleMusicService::getCharts(const std::string& genre, int limit) {
    std::vector<AppleMusicSong> songs;
    if (developerToken_.empty()) return songs;
    canMakeRequest();

    std::string endpoint = std::string(API_BASE) + "/catalog/" + storefront_ +
                           "/charts?types=songs&limit=" + std::to_string(limit);
    if (!genre.empty()) endpoint += "&genre=" + genre;

    std::string response = makeApiRequest(endpoint);
    if (response.empty()) return songs;

    try {
        auto j = json::parse(response);
        if (j.contains("results") && j["results"].contains("songs") &&
            !j["results"]["songs"].empty()) {
            auto& chartData = j["results"]["songs"][0];
            if (chartData.contains("data")) {
                for (const auto& item : chartData["data"]) {
                    songs.push_back(parseSong(item));
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("AppleMusicService: getCharts parse error: {}", e.what());
    }

    spdlog::info("AppleMusicService: Got {} chart songs", songs.size());
    return songs;
}

std::vector<Models::Playlist> AppleMusicService::getPlaylists() {
    std::vector<Models::Playlist> playlists;
    auto amPlaylists = getAppleMusicPlaylists();
    for (const auto& amp : amPlaylists) {
        Models::Playlist pl;
        pl.name = amp.name;
        pl.description = amp.description;
        playlists.push_back(pl);
    }
    return playlists;
}

std::vector<AppleMusicPlaylist> AppleMusicService::getAppleMusicPlaylists() {
    return getLibraryPlaylists();
}

std::vector<AppleMusicPlaylist> AppleMusicService::getCuratedPlaylists(const std::string& genre, int limit) {
    std::vector<AppleMusicPlaylist> playlists;
    if (developerToken_.empty()) return playlists;
    canMakeRequest();

    std::string endpoint = std::string(API_BASE) + "/catalog/" + storefront_ +
                           "/charts?types=playlists&limit=" + std::to_string(limit);
    if (!genre.empty()) endpoint += "&genre=" + genre;

    std::string response = makeApiRequest(endpoint);
    if (response.empty()) return playlists;

    try {
        auto j = json::parse(response);
        if (j.contains("results") && j["results"].contains("playlists") &&
            !j["results"]["playlists"].empty()) {
            auto& chartData = j["results"]["playlists"][0];
            if (chartData.contains("data")) {
                for (const auto& item : chartData["data"]) {
                    playlists.push_back(parsePlaylist(item));
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("AppleMusicService: getCuratedPlaylists parse error: {}", e.what());
    }
    return playlists;
}

AppleMusicPlaylist AppleMusicService::getPlaylistDetails(const std::string& playlistId) {
    AppleMusicPlaylist pl;
    if (developerToken_.empty()) return pl;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/catalog/" + storefront_ + "/playlists/" + playlistId);
    if (response.empty()) return pl;

    try {
        auto j = json::parse(response);
        if (j.contains("data") && !j["data"].empty()) {
            pl = parsePlaylist(j["data"][0]);
        }
    } catch (const std::exception& e) {
        spdlog::error("AppleMusicService: getPlaylistDetails parse error: {}", e.what());
    }
    return pl;
}

std::vector<AppleMusicSong> AppleMusicService::getPlaylistTracks(const std::string& playlistId) {
    std::vector<AppleMusicSong> tracks;
    if (developerToken_.empty()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/catalog/" + storefront_ + "/playlists/" + playlistId + "/tracks");
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        if (j.contains("data")) {
            for (const auto& item : j["data"]) {
                tracks.push_back(parseSong(item));
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("AppleMusicService: getPlaylistTracks parse error: {}", e.what());
    }
    return tracks;
}

std::vector<AppleMusicSong> AppleMusicService::getLibrarySongs(int limit, int offset) {
    std::vector<AppleMusicSong> songs;
    if (musicUserToken_.empty()) {
        spdlog::warn("AppleMusicService: No music user token for library access");
        return songs;
    }
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/me/library/songs?limit=" + std::to_string(limit) +
        "&offset=" + std::to_string(offset));
    if (response.empty()) return songs;

    try {
        auto j = json::parse(response);
        if (j.contains("data")) {
            for (const auto& item : j["data"]) {
                songs.push_back(parseSong(item));
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("AppleMusicService: getLibrarySongs parse error: {}", e.what());
    }
    return songs;
}

std::vector<AppleMusicPlaylist> AppleMusicService::getLibraryPlaylists(int limit, int offset) {
    std::vector<AppleMusicPlaylist> playlists;
    if (musicUserToken_.empty()) return playlists;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/me/library/playlists?limit=" + std::to_string(limit) +
        "&offset=" + std::to_string(offset));
    if (response.empty()) return playlists;

    try {
        auto j = json::parse(response);
        if (j.contains("data")) {
            for (const auto& item : j["data"]) {
                playlists.push_back(parsePlaylist(item));
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("AppleMusicService: getLibraryPlaylists parse error: {}", e.what());
    }
    return playlists;
}

std::string AppleMusicService::createLibraryPlaylist(const std::string& name,
                                                       const std::string& description) {
    if (musicUserToken_.empty()) return "";
    canMakeRequest();

    json body = {
        {"attributes", {
            {"name", name},
            {"description", description}
        }}
    };

    std::string response = makeApiPostRequest(
        std::string(API_BASE) + "/me/library/playlists", body.dump());
    if (response.empty()) return "";

    try {
        auto j = json::parse(response);
        if (j.contains("data") && !j["data"].empty()) {
            std::string playlistId = j["data"][0].value("id", "");
            spdlog::info("AppleMusicService: Created library playlist '{}' ({})", name, playlistId);
            return playlistId;
        }
    } catch (const std::exception& e) {
        spdlog::error("AppleMusicService: createLibraryPlaylist parse error: {}", e.what());
    }
    return "";
}

bool AppleMusicService::addSongsToLibrary(const std::vector<std::string>& songIds) {
    if (musicUserToken_.empty() || songIds.empty()) return false;
    canMakeRequest();

    json body = {{"ids", json::object()}};
    json ids = json::array();
    for (const auto& id : songIds) ids.push_back(id);
    body["ids"]["songs"] = ids;

    makeApiPostRequest(std::string(API_BASE) + "/me/library", body.dump());
    spdlog::info("AppleMusicService: Added {} songs to library", songIds.size());
    return true;
}

bool AppleMusicService::addTracksToPlaylist(const std::string& playlistId,
                                              const std::vector<std::string>& songIds) {
    if (musicUserToken_.empty() || songIds.empty()) return false;
    canMakeRequest();

    json data = json::array();
    for (const auto& id : songIds) {
        data.push_back({{"id", id}, {"type", "songs"}});
    }
    json body = {{"data", data}};

    makeApiPostRequest(
        std::string(API_BASE) + "/me/library/playlists/" + playlistId + "/tracks", body.dump());
    spdlog::info("AppleMusicService: Added {} tracks to playlist {}", songIds.size(), playlistId);
    return true;
}

std::vector<AppleMusicSong> AppleMusicService::getRecommendations(int limit) {
    std::vector<AppleMusicSong> songs;
    if (musicUserToken_.empty()) return songs;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/me/recommendations?limit=" + std::to_string(limit));
    if (response.empty()) return songs;

    try {
        auto j = json::parse(response);
        if (j.contains("data")) {
            for (const auto& rec : j["data"]) {
                if (rec.contains("relationships") && rec["relationships"].contains("contents") &&
                    rec["relationships"]["contents"].contains("data")) {
                    for (const auto& item : rec["relationships"]["contents"]["data"]) {
                        if (item.value("type", "") == "songs") {
                            songs.push_back(parseSong(item));
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("AppleMusicService: getRecommendations parse error: {}", e.what());
    }
    return songs;
}

std::vector<AppleMusicAlbum> AppleMusicService::getRecentlyAdded(int limit) {
    std::vector<AppleMusicAlbum> albums;
    if (musicUserToken_.empty()) return albums;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/me/library/recently-added?limit=" + std::to_string(limit));
    if (response.empty()) return albums;

    try {
        auto j = json::parse(response);
        if (j.contains("data")) {
            for (const auto& item : j["data"]) {
                if (item.value("type", "") == "library-albums" ||
                    item.value("type", "") == "albums") {
                    albums.push_back(parseAlbum(item));
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("AppleMusicService: getRecentlyAdded parse error: {}", e.what());
    }
    return albums;
}

std::string AppleMusicService::formatArtworkUrl(const std::string& templateUrl, int width, int height) {
    std::string url = templateUrl;
    size_t wPos = url.find("{w}");
    if (wPos != std::string::npos) url.replace(wPos, 3, std::to_string(width));
    size_t hPos = url.find("{h}");
    if (hPos != std::string::npos) url.replace(hPos, 3, std::to_string(height));
    return url;
}

AppleMusicSong AppleMusicService::parseSong(const json& item) const {
    AppleMusicSong song;
    song.id = item.value("id", "");

    if (item.contains("attributes")) {
        const auto& attrs = item["attributes"];
        song.name = attrs.value("name", "");
        song.artistName = attrs.value("artistName", "");
        song.albumName = attrs.value("albumName", "");
        song.isrc = attrs.value("isrc", "");
        song.durationMs = attrs.value("durationInMillis", 0);
        song.trackNumber = attrs.value("trackNumber", 0);
        song.discNumber = attrs.value("discNumber", 1);
        song.releaseDate = attrs.value("releaseDate", "");
        song.genreName = attrs.contains("genreNames") && !attrs["genreNames"].empty()
                             ? attrs["genreNames"][0].get<std::string>()
                             : "";
        song.composerName = attrs.value("composerName", "");
        song.contentRating = attrs.value("contentRating", "");
        song.isExplicit = (song.contentRating == "explicit");

        if (attrs.contains("artwork") && attrs["artwork"].contains("url")) {
            song.artworkUrl = attrs["artwork"]["url"].get<std::string>();
        }
        if (attrs.contains("previews") && !attrs["previews"].empty()) {
            song.previewUrl = attrs["previews"][0].value("url", "");
        }
        song.playCount = attrs.value("playParams", json{}).value("reporting", false) ? 0 : 0;
    }
    return song;
}

AppleMusicAlbum AppleMusicService::parseAlbum(const json& item) const {
    AppleMusicAlbum album;
    album.id = item.value("id", "");

    if (item.contains("attributes")) {
        const auto& attrs = item["attributes"];
        album.name = attrs.value("name", "");
        album.artistName = attrs.value("artistName", "");
        album.releaseDate = attrs.value("releaseDate", "");
        album.trackCount = attrs.value("trackCount", 0);
        album.genreName = attrs.contains("genreNames") && !attrs["genreNames"].empty()
                              ? attrs["genreNames"][0].get<std::string>()
                              : "";
        album.recordLabel = attrs.value("recordLabel", "");
        album.copyright = attrs.value("copyright", "");
        album.isComplete = attrs.value("isComplete", true);

        if (attrs.contains("artwork") && attrs["artwork"].contains("url")) {
            album.artworkUrl = attrs["artwork"]["url"].get<std::string>();
        }
    }
    return album;
}

AppleMusicPlaylist AppleMusicService::parsePlaylist(const json& item) const {
    AppleMusicPlaylist pl;
    pl.id = item.value("id", "");

    if (item.contains("attributes")) {
        const auto& attrs = item["attributes"];
        pl.name = attrs.value("name", "");
        pl.description = attrs.contains("description") && attrs["description"].contains("standard")
                             ? attrs["description"]["standard"].get<std::string>()
                             : "";
        pl.curatorName = attrs.value("curatorName", "");
        pl.lastModified = attrs.value("lastModifiedDate", "");

        if (attrs.contains("artwork") && attrs["artwork"].contains("url")) {
            pl.artworkUrl = attrs["artwork"]["url"].get<std::string>();
        }

        if (attrs.contains("playParams") && attrs["playParams"].contains("isLibrary")) {
            pl.isPublic = !attrs["playParams"]["isLibrary"].get<bool>();
        }
    }

    if (item.contains("relationships") && item["relationships"].contains("tracks") &&
        item["relationships"]["tracks"].contains("data")) {
        pl.trackCount = static_cast<int>(item["relationships"]["tracks"]["data"].size());
    }

    return pl;
}

std::string AppleMusicService::makeApiRequest(const std::string& endpoint) {
    juce::String headers = "Authorization: Bearer " + juce::String(developerToken_);
    if (!musicUserToken_.empty()) {
        headers += "\r\nMusic-User-Token: " + juce::String(musicUserToken_);
    }

    auto resp = httpGet(endpoint, headers);
    if (!resp.ok()) {
        spdlog::error("AppleMusicService: API error: status={} for {}", resp.status, endpoint);
        return {};
    }
    return resp.body;
}

std::string AppleMusicService::makeApiPostRequest(const std::string& endpoint, const std::string& body) {
    juce::String headers = "Authorization: Bearer " + juce::String(developerToken_) +
                           "\r\nContent-Type: application/json";
    if (!musicUserToken_.empty()) {
        headers += "\r\nMusic-User-Token: " + juce::String(musicUserToken_);
    }

    auto resp = httpSend(endpoint, "POST", body, headers);
    if (!resp.ok()) {
        spdlog::error("AppleMusicService: POST error: status={} for {}", resp.status, endpoint);
        return {};
    }
    return resp.body;
}

} // namespace BeatMate::Services::Streaming
