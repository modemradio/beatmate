#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

struct AmazonMusicArtist {
    std::string asin;               // Amazon Standard Identification Number
    std::string name;
    std::string url;
    std::string imageUrl;
    int popularity = 0;

    AmazonMusicArtist() = default;

    bool operator==(const AmazonMusicArtist& other) const { return asin == other.asin; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AmazonMusicArtist,
        asin, name, url, imageUrl, popularity
    )
};

struct AmazonMusicAlbum {
    std::string asin;
    std::string title;
    std::string url;
    std::string artworkUrl;
    std::string releaseDate;
    int trackCount = 0;
    std::string label;
    std::string genre;
    std::vector<AmazonMusicArtist> artists;
    bool isExplicit = false;
    std::string audioQuality;       // "SD", "HD", "ULTRA_HD"
    bool isSpatialAudio = false;

    AmazonMusicAlbum() = default;

    bool operator==(const AmazonMusicAlbum& other) const { return asin == other.asin; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AmazonMusicAlbum,
        asin, title, url, artworkUrl, releaseDate, trackCount,
        label, genre, artists, isExplicit, audioQuality, isSpatialAudio
    )
};

struct AmazonMusicTrack {
    std::string asin;
    std::string title;
    std::string url;
    std::string artistName;
    std::string albumName;
    int durationMs = 0;
    int trackNumber = 0;
    int discNumber = 1;
    std::string isrc;
    std::string artworkUrl;
    bool isExplicit = false;
    bool isPlayable = true;
    std::string audioQuality;       // "SD", "HD", "ULTRA_HD"
    bool isSpatialAudio = false;
    bool isDolbyAtmos = false;
    std::string genre;
    std::string releaseDate;
    int popularity = 0;
    std::string previewUrl;

    std::vector<AmazonMusicArtist> artists;
    AmazonMusicAlbum album;

    AmazonMusicTrack() = default;

    bool operator==(const AmazonMusicTrack& other) const { return asin == other.asin; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AmazonMusicTrack,
        asin, title, url, artistName, albumName, durationMs,
        trackNumber, discNumber, isrc, artworkUrl, isExplicit,
        isPlayable, audioQuality, isSpatialAudio, isDolbyAtmos,
        genre, releaseDate, popularity, previewUrl, artists, album
    )
};

struct AmazonMusicPlaylist {
    std::string asin;
    std::string title;
    std::string description;
    std::string url;
    std::string artworkUrl;
    std::string createdDate;
    std::string lastModifiedDate;
    int trackCount = 0;
    int durationMs = 0;
    std::string curatorName;
    bool isUserCreated = false;
    std::vector<AmazonMusicTrack> tracks;

    AmazonMusicPlaylist() = default;

    bool operator==(const AmazonMusicPlaylist& other) const { return asin == other.asin; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AmazonMusicPlaylist,
        asin, title, description, url, artworkUrl, createdDate,
        lastModifiedDate, trackCount, durationMs, curatorName,
        isUserCreated, tracks
    )
};

} // namespace BeatMate::Models
