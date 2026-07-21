#pragma once
#include <optional>
#include <string>
#include <vector>
#include "StreamingServiceBase.h"

namespace BeatMate::Services::Streaming {

struct BeatportTrack {
    int64_t id = 0;
    std::string name;
    std::string mixName;   // e.g. "Original Mix", "Extended Mix"
    std::string artistName;
    std::string remixerName;
    std::string labelName;
    int64_t labelId = 0;
    std::string genreName;
    int64_t genreId = 0;
    std::string subGenre;
    int durationMs = 0;
    int bpm = 0;
    std::string key;        // e.g. "Amin", "Cmaj"
    std::string releaseDate;
    std::string artworkUrl;
    std::string previewUrl;
    double price = 0.0;
    std::string currency;
    bool isExclusive = false;
    int chartPosition = 0;
    int64_t releaseId = 0;
    std::string releaseName;
};

struct BeatportGenre {
    int64_t id = 0;
    std::string name;
    std::string slug;
    std::vector<BeatportGenre> subGenres;
};

struct BeatportLabel {
    int64_t id = 0;
    std::string name;
    std::string slug;
    std::string artworkUrl;
    int trackCount = 0;
};

struct BeatportChart {
    int64_t id = 0;
    std::string name;
    std::string description;
    std::string djName;
    std::string artworkUrl;
    std::string genreName;
    std::string publishDate;
    int trackCount = 0;
    std::vector<BeatportTrack> tracks;
};

struct BeatportRelease {
    int64_t id = 0;
    std::string name;
    std::string artistName;
    std::string labelName;
    std::string releaseDate;
    std::string artworkUrl;
    std::string catalogNumber;
    int trackCount = 0;
    std::string type;  // "release", "single", "ep"
};

class BeatportService : public StreamingServiceBase {
public:
    BeatportService();
    ~BeatportService() override = default;

    bool authenticate(const std::string& clientId, const std::string& redirectUri,
                      const std::vector<std::string>& scopes) override;
    bool refreshAccessToken() override;

    StreamingSearchResult search(const std::string& query, int limit = 20, int offset = 0) override;
    std::vector<BeatportTrack> searchTracks(const std::string& query, int limit = 25, int offset = 0);
    std::vector<BeatportRelease> searchReleases(const std::string& query, int limit = 20);
    std::vector<BeatportLabel> searchLabels(const std::string& query, int limit = 20);

    std::optional<Models::StreamingTrack> getTrack(const std::string& trackId) override;
    BeatportTrack getBeatportTrack(const std::string& trackId);
    std::vector<BeatportTrack> getSimilarTracks(const std::string& trackId, int limit = 20);

    StreamingSearchResult getTopCharts(const std::string& genre = "", int limit = 100);
    std::vector<BeatportTrack> getTop100(const std::string& genreSlug = "");
    std::vector<BeatportChart> getDJCharts(int limit = 20, const std::string& genreSlug = "");
    BeatportChart getDJChartDetails(const std::string& chartId);
    std::vector<BeatportTrack> getHypeCharts(const std::string& genreSlug = "", int limit = 100);

    std::vector<BeatportGenre> getGenres();
    std::vector<BeatportTrack> getGenreTracks(const std::string& genreSlug, int limit = 50, int offset = 0);
    std::vector<BeatportRelease> getGenreReleases(const std::string& genreSlug, int limit = 20);

    BeatportLabel getLabel(const std::string& labelId);
    std::vector<BeatportTrack> getLabelTracks(const std::string& labelId, int limit = 50);
    std::vector<BeatportRelease> getLabelReleases(const std::string& labelId, int limit = 20);
    std::vector<BeatportLabel> getTopLabels(const std::string& genreSlug = "", int limit = 20);

    BeatportRelease getRelease(const std::string& releaseId);
    std::vector<BeatportTrack> getReleaseTracks(const std::string& releaseId);
    std::vector<BeatportRelease> getNewReleases(const std::string& genreSlug = "", int limit = 20);

    std::vector<Models::Playlist> getPlaylists() override;

    std::vector<BeatportTrack> getMyTracks(int limit = 50);
    std::vector<BeatportChart> getMyCharts();
    bool purchaseTrack(const std::string& trackId);

private:
    std::string makeApiRequest(const std::string& endpoint);

    BeatportTrack parseTrack(const nlohmann::json& j) const;
    BeatportGenre parseGenre(const nlohmann::json& j) const;
    BeatportLabel parseLabel(const nlohmann::json& j) const;
    BeatportChart parseChart(const nlohmann::json& j) const;
    BeatportRelease parseRelease(const nlohmann::json& j) const;

    std::string clientId_;
    std::string clientSecret_;

    static constexpr const char* API_BASE = "https://api.beatport.com/v4";
};

} // namespace BeatMate::Services::Streaming
