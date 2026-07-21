#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

struct SoundCloudUser {
    int64_t id = 0;
    std::string username;
    std::string displayName;
    std::string avatarUrl;
    std::string permalink;
    std::string uri;
    std::string city;
    std::string country;
    int followersCount = 0;
    int followingsCount = 0;
    int trackCount = 0;
    int playlistCount = 0;
    bool isVerified = false;
    std::string description;

    SoundCloudUser() = default;

    bool operator==(const SoundCloudUser& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SoundCloudUser,
        id, username, displayName, avatarUrl, permalink, uri,
        city, country, followersCount, followingsCount,
        trackCount, playlistCount, isVerified, description
    )
};

struct SoundCloudTrack {
    int64_t id = 0;
    std::string title;
    std::string description;
    std::string uri;
    std::string permalink;
    std::string permalinkUrl;
    std::string artworkUrl;
    std::string streamUrl;
    std::string downloadUrl;
    std::string waveformUrl;
    int durationMs = 0;
    std::string genre;
    std::string tagList;
    std::vector<std::string> tags;
    std::string license;
    std::string createdAt;
    int playbackCount = 0;
    int downloadCount = 0;
    int favoritingsCount = 0;
    int commentCount = 0;
    int repostsCount = 0;
    bool isDownloadable = false;
    bool isStreamable = true;
    bool isPublic = true;
    std::string sharing;            // "public" or "private"
    std::string state;              // "processing", "failed", "finished"
    float bpm = 0.0f;
    std::string keySignature;
    std::string isrc;
    std::string labelName;
    std::string release;
    std::string releaseDate;

    SoundCloudUser user;

    SoundCloudTrack() = default;

    bool operator==(const SoundCloudTrack& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SoundCloudTrack,
        id, title, description, uri, permalink, permalinkUrl,
        artworkUrl, streamUrl, downloadUrl, waveformUrl,
        durationMs, genre, tagList, tags, license, createdAt,
        playbackCount, downloadCount, favoritingsCount,
        commentCount, repostsCount, isDownloadable, isStreamable,
        isPublic, sharing, state, bpm, keySignature,
        isrc, labelName, release, releaseDate, user
    )
};

struct SoundCloudPlaylist {
    int64_t id = 0;
    std::string title;
    std::string description;
    std::string uri;
    std::string permalink;
    std::string permalinkUrl;
    std::string artworkUrl;
    std::string createdAt;
    int durationMs = 0;
    int trackCount = 0;
    bool isAlbum = false;
    std::string genre;
    std::string tagList;
    std::string sharing;
    SoundCloudUser user;
    std::vector<SoundCloudTrack> tracks;

    SoundCloudPlaylist() = default;

    bool operator==(const SoundCloudPlaylist& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SoundCloudPlaylist,
        id, title, description, uri, permalink, permalinkUrl,
        artworkUrl, createdAt, durationMs, trackCount,
        isAlbum, genre, tagList, sharing, user, tracks
    )
};

} // namespace BeatMate::Models
