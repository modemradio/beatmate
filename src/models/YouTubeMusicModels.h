#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

struct YouTubeMusicThumbnail {
    std::string url;
    int width = 0;
    int height = 0;

    YouTubeMusicThumbnail() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(YouTubeMusicThumbnail, url, width, height)
};

struct YouTubeMusicArtist {
    std::string id;
    std::string name;
    std::string channelId;
    std::vector<YouTubeMusicThumbnail> thumbnails;
    int subscriberCount = 0;

    YouTubeMusicArtist() = default;

    bool operator==(const YouTubeMusicArtist& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(YouTubeMusicArtist,
        id, name, channelId, thumbnails, subscriberCount
    )
};

struct YouTubeMusicAlbum {
    std::string id;
    std::string name;
    std::string browseId;
    std::string year;
    std::string type;
    std::vector<YouTubeMusicThumbnail> thumbnails;
    std::vector<YouTubeMusicArtist> artists;
    int trackCount = 0;
    int durationMs = 0;
    bool isExplicit = false;
    std::string description;

    YouTubeMusicAlbum() = default;

    bool operator==(const YouTubeMusicAlbum& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(YouTubeMusicAlbum,
        id, name, browseId, year, type, thumbnails, artists,
        trackCount, durationMs, isExplicit, description
    )
};

struct YouTubeMusicTrack {
    std::string videoId;
    std::string title;
    std::string artistName;
    std::string albumName;
    int durationMs = 0;
    std::string durationText;
    std::vector<YouTubeMusicThumbnail> thumbnails;
    std::vector<YouTubeMusicArtist> artists;
    YouTubeMusicAlbum album;
    bool isExplicit = false;
    bool isAvailable = true;
    std::string year;
    int likeCount = 0;
    int viewCount = 0;

    std::string setVideoId;
    std::string playlistId;
    std::string feedbackTokenAdd;
    std::string feedbackTokenRemove;
    std::string category;

    YouTubeMusicTrack() = default;

    bool operator==(const YouTubeMusicTrack& other) const { return videoId == other.videoId; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(YouTubeMusicTrack,
        videoId, title, artistName, albumName, durationMs, durationText,
        thumbnails, artists, album, isExplicit, isAvailable, year,
        likeCount, viewCount, setVideoId, playlistId,
        feedbackTokenAdd, feedbackTokenRemove, category
    )
};

struct YouTubeMusicPlaylist {
    std::string id;
    std::string title;
    std::string description;
    std::string privacy;
    std::vector<YouTubeMusicThumbnail> thumbnails;
    std::string authorName;
    std::string authorChannelId;
    int trackCount = 0;
    std::string duration;
    std::string year;
    std::vector<YouTubeMusicTrack> tracks;

    YouTubeMusicPlaylist() = default;

    bool operator==(const YouTubeMusicPlaylist& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(YouTubeMusicPlaylist,
        id, title, description, privacy, thumbnails,
        authorName, authorChannelId, trackCount, duration, year, tracks
    )
};

}
