#include "BeatportService.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <juce_core/juce_core.h>

using json = nlohmann::json;

namespace BeatMate::Services::Streaming {

BeatportService::BeatportService()
    : StreamingServiceBase("Beatport", Models::StreamingServiceType::Beatport) {
    setRateLimit(10);
}

bool BeatportService::authenticate(const std::string& clientId, const std::string& redirectUri,
                                    const std::vector<std::string>& scopes) {
    clientId_ = clientId;
    if (!scopes.empty()) clientSecret_ = scopes[0];
    spdlog::info("BeatportService: Starting OAuth2 authentication");

    juce::String postData =
        "grant_type=client_credentials"
        "&client_id=" + juce::URL::addEscapeChars(clientId_, true) +
        "&client_secret=" + juce::URL::addEscapeChars(clientSecret_, true);

    auto httpResp = httpSend("https://oauth.beatport.com/token", "POST",
                             postData.toStdString(),
                             "Content-Type: application/x-www-form-urlencoded");
    if (!httpResp.ok()) {
        spdlog::error("BeatportService: Auth failed (status={})", httpResp.status);
        return false;
    }

    try {
        auto resp = json::parse(httpResp.body);
        OAuthToken token;
        token.accessToken = resp["access_token"].get<std::string>();
        token.tokenType = resp.value("token_type", "Bearer");
        int expiresIn = resp.value("expires_in", 3600);
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        token.expiresAt = now + expiresIn;
        setToken(token);
        spdlog::info("BeatportService: Authenticated (token expires in {}s)", expiresIn);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("BeatportService: Auth parse error: {}", e.what());
        return false;
    }
}

bool BeatportService::refreshAccessToken() {
    return authenticate(clientId_, "", {clientSecret_});
}

StreamingSearchResult BeatportService::search(const std::string& query, int limit, int offset) {
    StreamingSearchResult result;
    auto tracks = searchTracks(query, limit, offset);
    for (const auto& bt : tracks) {
        Models::StreamingTrack t;
        t.serviceType = Models::StreamingServiceType::Beatport;
        t.serviceId = std::to_string(bt.id);
        t.durationMs = bt.durationMs;
        t.previewUrl = bt.previewUrl;
        t.artworkUrl = bt.artworkUrl;
        t.source = Models::TrackSource::Streaming;
        result.tracks.push_back(t);
    }
    result.offset = offset;
    result.limit = limit;
    return result;
}

std::vector<BeatportTrack> BeatportService::searchTracks(const std::string& query, int limit, int offset) {
    std::vector<BeatportTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    int page = offset / std::max(limit, 1) + 1;
    std::string response = makeApiRequest(
        std::string(API_BASE) + "/catalog/search?q=" +
        juce::URL::addEscapeChars(query, true).toStdString() +
        "&type=tracks&per_page=" + std::to_string(limit) +
        "&page=" + std::to_string(page));

    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        auto& items = j.contains("tracks") ? j["tracks"] : (j.contains("results") ? j["results"] : j);
        if (items.is_array()) {
            for (const auto& item : items) tracks.push_back(parseTrack(item));
        }
    } catch (const std::exception& e) {
        spdlog::error("BeatportService: searchTracks error: {}", e.what());
    }

    spdlog::info("BeatportService: Search '{}' returned {} tracks", query, tracks.size());
    return tracks;
}

std::vector<BeatportRelease> BeatportService::searchReleases(const std::string& query, int limit) {
    std::vector<BeatportRelease> releases;
    if (!isAuthenticated()) return releases;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/catalog/search?q=" +
        juce::URL::addEscapeChars(query, true).toStdString() +
        "&type=releases&per_page=" + std::to_string(limit));
    if (response.empty()) return releases;

    try {
        auto j = json::parse(response);
        auto& items = j.contains("releases") ? j["releases"] : j;
        if (items.is_array()) {
            for (const auto& item : items) releases.push_back(parseRelease(item));
        }
    } catch (...) {}
    return releases;
}

std::vector<BeatportLabel> BeatportService::searchLabels(const std::string& query, int limit) {
    std::vector<BeatportLabel> labels;
    if (!isAuthenticated()) return labels;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/catalog/search?q=" +
        juce::URL::addEscapeChars(query, true).toStdString() +
        "&type=labels&per_page=" + std::to_string(limit));
    if (response.empty()) return labels;

    try {
        auto j = json::parse(response);
        auto& items = j.contains("labels") ? j["labels"] : j;
        if (items.is_array()) {
            for (const auto& item : items) labels.push_back(parseLabel(item));
        }
    } catch (...) {}
    return labels;
}

std::optional<Models::StreamingTrack> BeatportService::getTrack(const std::string& trackId) {
    auto bt = getBeatportTrack(trackId);
    if (bt.id == 0) return std::nullopt;

    Models::StreamingTrack t;
    t.serviceType = Models::StreamingServiceType::Beatport;
    t.serviceId = std::to_string(bt.id);
    t.durationMs = bt.durationMs;
    t.previewUrl = bt.previewUrl;
    t.artworkUrl = bt.artworkUrl;
    t.source = Models::TrackSource::Streaming;
    return t;
}

BeatportTrack BeatportService::getBeatportTrack(const std::string& trackId) {
    BeatportTrack track;
    if (!isAuthenticated()) return track;
    canMakeRequest();

    std::string response = makeApiRequest(std::string(API_BASE) + "/catalog/tracks/" + trackId);
    if (response.empty()) return track;
    try { track = parseTrack(json::parse(response)); } catch (...) {}
    return track;
}

std::vector<BeatportTrack> BeatportService::getSimilarTracks(const std::string& trackId, int limit) {
    std::vector<BeatportTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/catalog/tracks/" + trackId + "/similar?per_page=" + std::to_string(limit));
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        auto& items = j.contains("results") ? j["results"] : j;
        if (items.is_array()) {
            for (const auto& item : items) tracks.push_back(parseTrack(item));
        }
    } catch (...) {}
    return tracks;
}

StreamingSearchResult BeatportService::getTopCharts(const std::string& genre, int limit) {
    StreamingSearchResult result;
    auto tracks = getTop100(genre);
    for (const auto& bt : tracks) {
        Models::StreamingTrack t;
        t.serviceType = Models::StreamingServiceType::Beatport;
        t.serviceId = std::to_string(bt.id);
        t.durationMs = bt.durationMs;
        t.previewUrl = bt.previewUrl;
        t.artworkUrl = bt.artworkUrl;
        t.source = Models::TrackSource::Streaming;
        result.tracks.push_back(t);
        if (static_cast<int>(result.tracks.size()) >= limit) break;
    }
    result.totalResults = static_cast<int>(result.tracks.size());
    return result;
}

std::vector<BeatportTrack> BeatportService::getTop100(const std::string& genreSlug) {
    std::vector<BeatportTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string endpoint = std::string(API_BASE) + "/catalog/tracks?per_page=100&ordering=-popularity";
    if (!genreSlug.empty()) endpoint += "&genre_slug=" + genreSlug;

    std::string response = makeApiRequest(endpoint);
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        auto& items = j.contains("results") ? j["results"] : j;
        if (items.is_array()) {
            int pos = 1;
            for (const auto& item : items) {
                auto t = parseTrack(item);
                t.chartPosition = pos++;
                tracks.push_back(t);
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("BeatportService: getTop100 error: {}", e.what());
    }

    spdlog::info("BeatportService: Top 100 ({}) returned {} tracks",
                 genreSlug.empty() ? "all" : genreSlug, tracks.size());
    return tracks;
}

std::vector<BeatportChart> BeatportService::getDJCharts(int limit, const std::string& genreSlug) {
    std::vector<BeatportChart> charts;
    if (!isAuthenticated()) return charts;
    canMakeRequest();

    std::string endpoint = std::string(API_BASE) + "/catalog/charts?per_page=" + std::to_string(limit);
    if (!genreSlug.empty()) endpoint += "&genre_slug=" + genreSlug;

    std::string response = makeApiRequest(endpoint);
    if (response.empty()) return charts;

    try {
        auto j = json::parse(response);
        auto& items = j.contains("results") ? j["results"] : j;
        if (items.is_array()) {
            for (const auto& item : items) charts.push_back(parseChart(item));
        }
    } catch (...) {}

    spdlog::info("BeatportService: Got {} DJ charts", charts.size());
    return charts;
}

BeatportChart BeatportService::getDJChartDetails(const std::string& chartId) {
    BeatportChart chart;
    if (!isAuthenticated()) return chart;
    canMakeRequest();

    std::string response = makeApiRequest(std::string(API_BASE) + "/catalog/charts/" + chartId);
    if (response.empty()) return chart;
    try { chart = parseChart(json::parse(response)); } catch (...) {}

    canMakeRequest();
    std::string tracksResp = makeApiRequest(
        std::string(API_BASE) + "/catalog/charts/" + chartId + "/tracks");
    if (!tracksResp.empty()) {
        try {
            auto j = json::parse(tracksResp);
            auto& items = j.contains("results") ? j["results"] : j;
            if (items.is_array()) {
                for (const auto& item : items) chart.tracks.push_back(parseTrack(item));
            }
        } catch (...) {}
    }
    return chart;
}

std::vector<BeatportTrack> BeatportService::getHypeCharts(const std::string& genreSlug, int limit) {
    std::vector<BeatportTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string endpoint = std::string(API_BASE) + "/catalog/tracks?per_page=" + std::to_string(limit) +
                           "&ordering=-hype_score";
    if (!genreSlug.empty()) endpoint += "&genre_slug=" + genreSlug;

    std::string response = makeApiRequest(endpoint);
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        auto& items = j.contains("results") ? j["results"] : j;
        if (items.is_array()) {
            for (const auto& item : items) tracks.push_back(parseTrack(item));
        }
    } catch (...) {}
    return tracks;
}

std::vector<BeatportGenre> BeatportService::getGenres() {
    std::vector<BeatportGenre> genres;
    if (!isAuthenticated()) return genres;
    canMakeRequest();

    std::string response = makeApiRequest(std::string(API_BASE) + "/catalog/genres");
    if (response.empty()) return genres;

    try {
        auto j = json::parse(response);
        auto& items = j.contains("results") ? j["results"] : j;
        if (items.is_array()) {
            for (const auto& item : items) genres.push_back(parseGenre(item));
        }
    } catch (...) {}

    spdlog::info("BeatportService: Got {} genres", genres.size());
    return genres;
}

std::vector<BeatportTrack> BeatportService::getGenreTracks(const std::string& genreSlug,
                                                             int limit, int offset) {
    std::vector<BeatportTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    int page = offset / std::max(limit, 1) + 1;
    std::string response = makeApiRequest(
        std::string(API_BASE) + "/catalog/tracks?genre_slug=" + genreSlug +
        "&per_page=" + std::to_string(limit) + "&page=" + std::to_string(page));
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        auto& items = j.contains("results") ? j["results"] : j;
        if (items.is_array()) {
            for (const auto& item : items) tracks.push_back(parseTrack(item));
        }
    } catch (...) {}
    return tracks;
}

std::vector<BeatportRelease> BeatportService::getGenreReleases(const std::string& genreSlug, int limit) {
    std::vector<BeatportRelease> releases;
    if (!isAuthenticated()) return releases;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/catalog/releases?genre_slug=" + genreSlug +
        "&per_page=" + std::to_string(limit));
    if (response.empty()) return releases;

    try {
        auto j = json::parse(response);
        auto& items = j.contains("results") ? j["results"] : j;
        if (items.is_array()) {
            for (const auto& item : items) releases.push_back(parseRelease(item));
        }
    } catch (...) {}
    return releases;
}

BeatportLabel BeatportService::getLabel(const std::string& labelId) {
    BeatportLabel label;
    if (!isAuthenticated()) return label;
    canMakeRequest();
    std::string response = makeApiRequest(std::string(API_BASE) + "/catalog/labels/" + labelId);
    if (response.empty()) return label;
    try { label = parseLabel(json::parse(response)); } catch (...) {}
    return label;
}

std::vector<BeatportTrack> BeatportService::getLabelTracks(const std::string& labelId, int limit) {
    std::vector<BeatportTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/catalog/tracks?label_id=" + labelId +
        "&per_page=" + std::to_string(limit));
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        auto& items = j.contains("results") ? j["results"] : j;
        if (items.is_array()) {
            for (const auto& item : items) tracks.push_back(parseTrack(item));
        }
    } catch (...) {}
    return tracks;
}

std::vector<BeatportRelease> BeatportService::getLabelReleases(const std::string& labelId, int limit) {
    std::vector<BeatportRelease> releases;
    if (!isAuthenticated()) return releases;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/catalog/releases?label_id=" + labelId +
        "&per_page=" + std::to_string(limit));
    if (response.empty()) return releases;

    try {
        auto j = json::parse(response);
        auto& items = j.contains("results") ? j["results"] : j;
        if (items.is_array()) {
            for (const auto& item : items) releases.push_back(parseRelease(item));
        }
    } catch (...) {}
    return releases;
}

std::vector<BeatportLabel> BeatportService::getTopLabels(const std::string& genreSlug, int limit) {
    std::vector<BeatportLabel> labels;
    if (!isAuthenticated()) return labels;
    canMakeRequest();

    std::string endpoint = std::string(API_BASE) + "/catalog/labels?per_page=" + std::to_string(limit) +
                           "&ordering=-popularity";
    if (!genreSlug.empty()) endpoint += "&genre_slug=" + genreSlug;

    std::string response = makeApiRequest(endpoint);
    if (response.empty()) return labels;

    try {
        auto j = json::parse(response);
        auto& items = j.contains("results") ? j["results"] : j;
        if (items.is_array()) {
            for (const auto& item : items) labels.push_back(parseLabel(item));
        }
    } catch (...) {}
    return labels;
}

BeatportRelease BeatportService::getRelease(const std::string& releaseId) {
    BeatportRelease release;
    if (!isAuthenticated()) return release;
    canMakeRequest();
    std::string response = makeApiRequest(std::string(API_BASE) + "/catalog/releases/" + releaseId);
    if (response.empty()) return release;
    try { release = parseRelease(json::parse(response)); } catch (...) {}
    return release;
}

std::vector<BeatportTrack> BeatportService::getReleaseTracks(const std::string& releaseId) {
    std::vector<BeatportTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/catalog/releases/" + releaseId + "/tracks");
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        auto& items = j.contains("results") ? j["results"] : j;
        if (items.is_array()) {
            for (const auto& item : items) tracks.push_back(parseTrack(item));
        }
    } catch (...) {}
    return tracks;
}

std::vector<BeatportRelease> BeatportService::getNewReleases(const std::string& genreSlug, int limit) {
    std::vector<BeatportRelease> releases;
    if (!isAuthenticated()) return releases;
    canMakeRequest();

    std::string endpoint = std::string(API_BASE) + "/catalog/releases?per_page=" + std::to_string(limit) +
                           "&ordering=-publish_date";
    if (!genreSlug.empty()) endpoint += "&genre_slug=" + genreSlug;

    std::string response = makeApiRequest(endpoint);
    if (response.empty()) return releases;

    try {
        auto j = json::parse(response);
        auto& items = j.contains("results") ? j["results"] : j;
        if (items.is_array()) {
            for (const auto& item : items) releases.push_back(parseRelease(item));
        }
    } catch (...) {}
    return releases;
}

std::vector<Models::Playlist> BeatportService::getPlaylists() { return {}; }

std::vector<BeatportTrack> BeatportService::getMyTracks(int limit) {
    std::vector<BeatportTrack> tracks;
    if (!isAuthenticated()) return tracks;
    canMakeRequest();

    std::string response = makeApiRequest(
        std::string(API_BASE) + "/my/tracks?per_page=" + std::to_string(limit));
    if (response.empty()) return tracks;

    try {
        auto j = json::parse(response);
        auto& items = j.contains("results") ? j["results"] : j;
        if (items.is_array()) {
            for (const auto& item : items) tracks.push_back(parseTrack(item));
        }
    } catch (...) {}
    return tracks;
}

std::vector<BeatportChart> BeatportService::getMyCharts() {
    std::vector<BeatportChart> charts;
    if (!isAuthenticated()) return charts;
    canMakeRequest();

    std::string response = makeApiRequest(std::string(API_BASE) + "/my/charts");
    if (response.empty()) return charts;

    try {
        auto j = json::parse(response);
        auto& items = j.contains("results") ? j["results"] : j;
        if (items.is_array()) {
            for (const auto& item : items) charts.push_back(parseChart(item));
        }
    } catch (...) {}
    return charts;
}

bool BeatportService::purchaseTrack(const std::string& trackId) {
    spdlog::info("BeatportService: Purchase request for track {} (redirect to web)", trackId);
    return false;
}

BeatportTrack BeatportService::parseTrack(const json& j) const {
    BeatportTrack t;
    t.id = j.value("id", static_cast<int64_t>(0));
    t.name = j.value("name", j.value("title", ""));
    t.mixName = j.value("mix_name", j.value("mix", ""));
    t.durationMs = j.contains("length_ms") ? j["length_ms"].get<int>()
                   : (j.value("length", 0) * 1000);
    t.bpm = static_cast<int>(j.value("bpm", 0.0));
    t.releaseDate = j.value("publish_date", j.value("release_date", ""));
    t.previewUrl = j.value("sample_url", j.value("preview", ""));
    t.isExclusive = j.value("exclusive", false);
    t.price = j.value("price", json{}).value("value", 0.0);
    t.currency = j.value("price", json{}).value("code", "USD");

    if (j.contains("artists") && !j["artists"].empty()) {
        t.artistName = j["artists"][0].value("name", "");
    }
    if (j.contains("remixers") && !j["remixers"].empty()) {
        t.remixerName = j["remixers"][0].value("name", "");
    }
    if (j.contains("label")) {
        t.labelName = j["label"].value("name", "");
        t.labelId = j["label"].value("id", static_cast<int64_t>(0));
    }
    if (j.contains("genre")) {
        t.genreName = j["genre"].value("name", "");
        t.genreId = j["genre"].value("id", static_cast<int64_t>(0));
    }
    if (j.contains("sub_genre")) {
        t.subGenre = j["sub_genre"].value("name", "");
    }
    if (j.contains("key")) {
        t.key = j["key"].value("name", j["key"].value("short_name", ""));
    }
    if (j.contains("image")) {
        t.artworkUrl = j["image"].value("uri", j["image"].value("url", ""));
    } else if (j.contains("release") && j["release"].contains("image")) {
        t.artworkUrl = j["release"]["image"].value("uri", "");
    }
    if (j.contains("release")) {
        t.releaseId = j["release"].value("id", static_cast<int64_t>(0));
        t.releaseName = j["release"].value("name", "");
    }
    return t;
}

BeatportGenre BeatportService::parseGenre(const json& j) const {
    BeatportGenre g;
    g.id = j.value("id", static_cast<int64_t>(0));
    g.name = j.value("name", "");
    g.slug = j.value("slug", "");
    if (j.contains("sub_genres") && j["sub_genres"].is_array()) {
        for (const auto& sub : j["sub_genres"]) g.subGenres.push_back(parseGenre(sub));
    }
    return g;
}

BeatportLabel BeatportService::parseLabel(const json& j) const {
    BeatportLabel l;
    l.id = j.value("id", static_cast<int64_t>(0));
    l.name = j.value("name", "");
    l.slug = j.value("slug", "");
    l.trackCount = j.value("track_count", 0);
    if (j.contains("image")) {
        l.artworkUrl = j["image"].value("uri", j["image"].value("url", ""));
    }
    return l;
}

BeatportChart BeatportService::parseChart(const json& j) const {
    BeatportChart c;
    c.id = j.value("id", static_cast<int64_t>(0));
    c.name = j.value("name", j.value("title", ""));
    c.description = j.value("description", "");
    c.publishDate = j.value("publish_date", "");
    c.trackCount = j.value("track_count", 0);

    if (j.contains("person")) c.djName = j["person"].value("name", "");
    if (j.contains("genre")) c.genreName = j["genre"].value("name", "");
    if (j.contains("image")) {
        c.artworkUrl = j["image"].value("uri", j["image"].value("url", ""));
    }
    return c;
}

BeatportRelease BeatportService::parseRelease(const json& j) const {
    BeatportRelease r;
    r.id = j.value("id", static_cast<int64_t>(0));
    r.name = j.value("name", j.value("title", ""));
    r.releaseDate = j.value("publish_date", j.value("release_date", ""));
    r.catalogNumber = j.value("catalog_number", "");
    r.trackCount = j.value("track_count", 0);
    r.type = j.value("type", j.value("type_name", ""));

    if (j.contains("artists") && !j["artists"].empty()) {
        r.artistName = j["artists"][0].value("name", "");
    }
    if (j.contains("label")) r.labelName = j["label"].value("name", "");
    if (j.contains("image")) {
        r.artworkUrl = j["image"].value("uri", j["image"].value("url", ""));
    }
    return r;
}

std::string BeatportService::makeApiRequest(const std::string& endpoint) {
    auto resp = httpGet(endpoint, "Authorization: " + juce::String(getAuthHeader()));
    if (!resp.ok()) {
        spdlog::error("BeatportService: API error: status={} for {}", resp.status, endpoint);
        return {};
    }
    return resp.body;
}

} // namespace BeatMate::Services::Streaming
