#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

struct TidalArtist {
    int64_t id = 0;
    std::string name;
    std::string url;
    std::string pictureUrl;
    int popularity = 0;
    std::string artistType;         // "MAIN", "FEATURED"

    TidalArtist() = default;

    bool operator==(const TidalArtist& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(TidalArtist,
        id, name, url, pictureUrl, popularity, artistType
    )
};

struct TidalAlbum {
    int64_t id = 0;
    std::string title;
    std::string url;
    std::string coverUrl;
    std::string releaseDate;
    int numberOfTracks = 0;
    int numberOfVolumes = 1;
    int duration = 0;               // seconds
    std::string copyright;
    std::string audioQuality;       // "LOW", "HIGH", "LOSSLESS", "HI_RES"
    bool isExplicit = false;
    std::vector<TidalArtist> artists;
    std::string upc;
    int popularity = 0;

    TidalAlbum() = default;

    bool operator==(const TidalAlbum& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(TidalAlbum,
        id, title, url, coverUrl, releaseDate, numberOfTracks,
        numberOfVolumes, duration, copyright, audioQuality,
        isExplicit, artists, upc, popularity
    )
};

struct TidalTrack {
    int64_t id = 0;
    std::string title;
    std::string url;
    int duration = 0;               // seconds
    int trackNumber = 0;
    int volumeNumber = 1;
    std::string isrc;
    bool isExplicit = false;
    std::string audioQuality;       // "LOW", "HIGH", "LOSSLESS", "HI_RES", "HI_RES_LOSSLESS"
    std::string audioModes;         // "STEREO", "DOLBY_ATMOS", "SONY_360RA"
    bool isAvailable = true;
    std::string copyright;
    std::string streamStartDate;
    int popularity = 0;
    float replayGain = 0.0f;
    float peakAmplitude = 0.0f;

    std::vector<TidalArtist> artists;
    TidalAlbum album;

    bool isMasterQuality = false;   // MQA
    bool isDolbyAtmos = false;
    bool isSony360RA = false;
    std::string previewUrl;

    TidalTrack() = default;

    bool operator==(const TidalTrack& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(TidalTrack,
        id, title, url, duration, trackNumber, volumeNumber,
        isrc, isExplicit, audioQuality, audioModes, isAvailable,
        copyright, streamStartDate, popularity, replayGain, peakAmplitude,
        artists, album, isMasterQuality, isDolbyAtmos, isSony360RA, previewUrl
    )
};

struct TidalPlaylist {
    std::string uuid;
    std::string title;
    std::string description;
    std::string url;
    std::string imageUrl;
    std::string created;
    std::string lastUpdated;
    int numberOfTracks = 0;
    int duration = 0;               // total duration in seconds
    std::string type;               // "USER", "EDITORIAL"
    bool isPublic = false;
    std::string creatorId;
    std::string creatorName;
    std::vector<TidalTrack> tracks;

    TidalPlaylist() = default;

    bool operator==(const TidalPlaylist& other) const { return uuid == other.uuid; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(TidalPlaylist,
        uuid, title, description, url, imageUrl, created, lastUpdated,
        numberOfTracks, duration, type, isPublic, creatorId, creatorName, tracks
    )
};

} // namespace BeatMate::Models
