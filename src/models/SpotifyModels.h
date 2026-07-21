#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

struct SpotifyImage {
    std::string url;
    int width = 0;
    int height = 0;

    SpotifyImage() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SpotifyImage, url, width, height)
};

struct SpotifyArtist {
    std::string id;
    std::string name;
    std::string uri;
    std::string href;
    std::vector<std::string> genres;
    int popularity = 0;
    std::vector<SpotifyImage> images;
    int followers = 0;
    std::string externalUrl;

    SpotifyArtist() = default;

    bool operator==(const SpotifyArtist& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SpotifyArtist,
        id, name, uri, href, genres, popularity, images, followers, externalUrl
    )
};

struct SpotifyAlbum {
    std::string id;
    std::string name;
    std::string uri;
    std::string href;
    std::string albumType;          // "album", "single", "compilation"
    std::string releaseDate;
    std::string releaseDatePrecision; // "year", "month", "day"
    int totalTracks = 0;
    std::vector<SpotifyImage> images;
    std::vector<SpotifyArtist> artists;
    std::string externalUrl;
    std::string label;
    std::vector<std::string> genres;
    int popularity = 0;
    std::vector<std::string> availableMarkets;
    std::string copyrights;

    SpotifyAlbum() = default;

    bool operator==(const SpotifyAlbum& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SpotifyAlbum,
        id, name, uri, href, albumType, releaseDate, releaseDatePrecision,
        totalTracks, images, artists, externalUrl, label, genres, popularity,
        availableMarkets, copyrights
    )
};

struct SpotifyAudioFeatures {
    std::string id;
    float danceability = 0.0f;      // 0-1
    float energy = 0.0f;            // 0-1
    int key = 0;                    // 0-11 (pitch class)
    float loudness = 0.0f;          // dB
    int mode = 0;                   // 0=minor, 1=major
    float speechiness = 0.0f;       // 0-1
    float acousticness = 0.0f;      // 0-1
    float instrumentalness = 0.0f;  // 0-1
    float liveness = 0.0f;          // 0-1
    float valence = 0.0f;           // 0-1
    float tempo = 0.0f;             // BPM
    int durationMs = 0;
    int timeSignature = 4;

    SpotifyAudioFeatures() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SpotifyAudioFeatures,
        id, danceability, energy, key, loudness, mode,
        speechiness, acousticness, instrumentalness, liveness,
        valence, tempo, durationMs, timeSignature
    )
};

struct SpotifyTrack {
    std::string id;
    std::string name;
    std::string uri;
    std::string href;
    int durationMs = 0;
    int trackNumber = 0;
    int discNumber = 1;
    bool isExplicit = false;
    bool isPlayable = true;
    int popularity = 0;
    std::string previewUrl;
    std::string isrc;
    std::string externalUrl;

    std::vector<SpotifyArtist> artists;
    SpotifyAlbum album;
    SpotifyAudioFeatures audioFeatures;

    std::vector<std::string> availableMarkets;
    bool isLocal = false;

    std::string linkedFromId;

    SpotifyTrack() = default;

    bool operator==(const SpotifyTrack& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SpotifyTrack,
        id, name, uri, href, durationMs, trackNumber, discNumber,
        isExplicit, isPlayable, popularity, previewUrl, isrc, externalUrl,
        artists, album, audioFeatures, availableMarkets, isLocal, linkedFromId
    )
};

struct SpotifyPlaylist {
    std::string id;
    std::string name;
    std::string description;
    std::string uri;
    std::string href;
    std::string snapshotId;
    bool isPublic = false;
    bool isCollaborative = false;
    std::vector<SpotifyImage> images;
    std::string ownerId;
    std::string ownerDisplayName;
    int totalTracks = 0;
    std::string externalUrl;

    std::vector<SpotifyTrack> tracks;

    SpotifyPlaylist() = default;

    bool operator==(const SpotifyPlaylist& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SpotifyPlaylist,
        id, name, description, uri, href, snapshotId,
        isPublic, isCollaborative, images, ownerId, ownerDisplayName,
        totalTracks, externalUrl, tracks
    )
};

} // namespace BeatMate::Models
