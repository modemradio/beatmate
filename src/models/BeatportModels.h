#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

struct BeatportGenre {
    int64_t id = 0;
    std::string name;
    std::string slug;
    int64_t parentId = 0;

    BeatportGenre() = default;

    bool operator==(const BeatportGenre& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BeatportGenre, id, name, slug, parentId)
};

struct BeatportLabel {
    int64_t id = 0;
    std::string name;
    std::string slug;
    std::string url;
    std::string imageUrl;

    BeatportLabel() = default;

    bool operator==(const BeatportLabel& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BeatportLabel, id, name, slug, url, imageUrl)
};

struct BeatportArtist {
    int64_t id = 0;
    std::string name;
    std::string slug;
    std::string url;
    std::string imageUrl;
    std::vector<BeatportGenre> genres;

    BeatportArtist() = default;

    bool operator==(const BeatportArtist& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BeatportArtist,
        id, name, slug, url, imageUrl, genres
    )
};

struct BeatportRelease {
    int64_t id = 0;
    std::string name;
    std::string slug;
    std::string url;
    std::string artworkUrl;
    std::string releaseDate;
    std::string catalogNumber;
    BeatportLabel label;
    std::vector<BeatportArtist> artists;
    int trackCount = 0;
    std::string type;               // "release", "single", "ep", "compilation"
    std::string upc;
    std::string description;
    bool isExclusive = false;

    BeatportRelease() = default;

    bool operator==(const BeatportRelease& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BeatportRelease,
        id, name, slug, url, artworkUrl, releaseDate, catalogNumber,
        label, artists, trackCount, type, upc, description, isExclusive
    )
};

struct BeatportTrack {
    int64_t id = 0;
    std::string name;
    std::string mixName;            // "Original Mix", "Club Mix", etc.
    std::string slug;
    std::string url;
    std::string artworkUrl;
    std::string previewUrl;
    int durationMs = 0;
    float bpm = 0.0f;
    std::string key;
    std::string isrc;
    std::string releaseDate;
    std::string publishDate;
    bool isExclusive = false;
    bool isAvailableForStreaming = true;

    float price = 0.0f;
    std::string currency;           // "USD", "EUR", etc.

    int chartPosition = 0;         // 0 = not charting
    std::string chartName;
    int previousChartPosition = 0;
    int weeksOnChart = 0;

    std::vector<BeatportArtist> artists;
    std::vector<BeatportArtist> remixers;
    BeatportRelease release;
    BeatportLabel label;
    BeatportGenre genre;
    BeatportGenre subGenre;

    BeatportTrack() = default;

    bool operator==(const BeatportTrack& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BeatportTrack,
        id, name, mixName, slug, url, artworkUrl, previewUrl,
        durationMs, bpm, key, isrc, releaseDate, publishDate,
        isExclusive, isAvailableForStreaming,
        price, currency,
        chartPosition, chartName, previousChartPosition, weeksOnChart,
        artists, remixers, release, label, genre, subGenre
    )
};

struct BeatportChart {
    int64_t id = 0;
    std::string name;
    std::string slug;
    std::string url;
    std::string description;
    std::string publishDate;
    BeatportGenre genre;
    std::vector<BeatportTrack> tracks;
    BeatportArtist dj;              // chart creator

    BeatportChart() = default;

    bool operator==(const BeatportChart& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BeatportChart,
        id, name, slug, url, description, publishDate, genre, tracks, dj
    )
};

} // namespace BeatMate::Models
