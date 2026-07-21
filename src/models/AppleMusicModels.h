#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

struct AppleMusicArtwork {
    std::string url;
    int width = 0;
    int height = 0;
    std::string bgColor;
    std::string textColor1;
    std::string textColor2;

    AppleMusicArtwork() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AppleMusicArtwork,
        url, width, height, bgColor, textColor1, textColor2
    )
};

struct AppleMusicArtist {
    std::string id;
    std::string name;
    std::string url;
    std::vector<std::string> genreNames;
    AppleMusicArtwork artwork;
    std::string editorialNotes;

    AppleMusicArtist() = default;

    bool operator==(const AppleMusicArtist& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AppleMusicArtist,
        id, name, url, genreNames, artwork, editorialNotes
    )
};

struct AppleMusicAlbum {
    std::string id;
    std::string name;
    std::string url;
    std::string artistName;
    std::string releaseDate;
    int trackCount = 0;
    std::vector<std::string> genreNames;
    AppleMusicArtwork artwork;
    std::string recordLabel;
    std::string copyright;
    bool isSingle = false;
    bool isComplete = true;
    std::string contentRating;       // "clean", "explicit", or empty
    std::string editorialNotes;
    std::string upc;

    AppleMusicAlbum() = default;

    bool operator==(const AppleMusicAlbum& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AppleMusicAlbum,
        id, name, url, artistName, releaseDate, trackCount,
        genreNames, artwork, recordLabel, copyright,
        isSingle, isComplete, contentRating, editorialNotes, upc
    )
};

struct AppleMusicTrack {
    std::string id;
    std::string name;
    std::string url;
    std::string artistName;
    std::string albumName;
    int durationMs = 0;
    int trackNumber = 0;
    int discNumber = 1;
    std::string isrc;
    std::vector<std::string> genreNames;
    AppleMusicArtwork artwork;
    std::string releaseDate;
    std::string composerName;
    std::string contentRating;
    bool hasLyrics = false;
    std::string previewUrl;
    int popularity = 0;

    std::string playParams;         // Apple Music play parameters
    bool isAppleDigitalMaster = false;
    std::string audioLocale;

    AppleMusicTrack() = default;

    bool operator==(const AppleMusicTrack& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AppleMusicTrack,
        id, name, url, artistName, albumName, durationMs,
        trackNumber, discNumber, isrc, genreNames, artwork,
        releaseDate, composerName, contentRating, hasLyrics,
        previewUrl, popularity, playParams, isAppleDigitalMaster, audioLocale
    )
};

struct AppleMusicPlaylist {
    std::string id;
    std::string name;
    std::string description;
    std::string url;
    std::string curatorName;
    AppleMusicArtwork artwork;
    std::string lastModifiedDate;
    bool isChart = false;
    int trackCount = 0;
    std::vector<AppleMusicTrack> tracks;

    AppleMusicPlaylist() = default;

    bool operator==(const AppleMusicPlaylist& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AppleMusicPlaylist,
        id, name, description, url, curatorName, artwork,
        lastModifiedDate, isChart, trackCount, tracks
    )
};

} // namespace BeatMate::Models
