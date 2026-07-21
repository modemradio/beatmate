#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>
#include "DJSoftwareTrack.h"

namespace BeatMate::Models {

enum class StreamingServiceType : int {
    Spotify = 0,
    AppleMusic = 1,
    SoundCloud = 2,
    Tidal = 3,
    YouTubeMusic = 4,
    AmazonMusic = 5,
    Beatport = 6,
    Deezer = 7,
    Bandcamp = 8
};

NLOHMANN_JSON_SERIALIZE_ENUM(StreamingServiceType, {
    { StreamingServiceType::Spotify, "Spotify" },
    { StreamingServiceType::AppleMusic, "AppleMusic" },
    { StreamingServiceType::SoundCloud, "SoundCloud" },
    { StreamingServiceType::Tidal, "Tidal" },
    { StreamingServiceType::YouTubeMusic, "YouTubeMusic" },
    { StreamingServiceType::AmazonMusic, "AmazonMusic" },
    { StreamingServiceType::Beatport, "Beatport" },
    { StreamingServiceType::Deezer, "Deezer" },
    { StreamingServiceType::Bandcamp, "Bandcamp" }
})

struct StreamingTrack : public DJSoftwareTrack {
    StreamingServiceType serviceType = StreamingServiceType::Spotify;
    std::string serviceId;
    std::string previewUrl;
    std::string artworkUrl;
    std::string isrc;
    int popularity = 0;

    std::string streamingUrl;
    bool isAvailable = true;        // availability in user's region
    std::string externalUrl;
    int durationMs = 0;
    bool isExplicit = false;
    int discNumber = 1;
    int trackNumber = 1;

    StreamingTrack() {
        source = TrackSource::Streaming;
    }

    StreamingTrack(StreamingServiceType serviceType, const std::string& serviceId)
        : serviceType(serviceType), serviceId(serviceId) {
        source = TrackSource::Streaming;
    }

    friend void to_json(nlohmann::json& j, const StreamingTrack& t) {
        j = nlohmann::json{
            {"localTrackId", t.localTrackId},
            {"source", t.source},
            {"externalId", t.externalId},
            {"externalPath", t.externalPath},
            {"syncedAt", t.syncedAt},
            {"serviceType", t.serviceType},
            {"serviceId", t.serviceId},
            {"previewUrl", t.previewUrl},
            {"artworkUrl", t.artworkUrl},
            {"isrc", t.isrc},
            {"popularity", t.popularity},
            {"streamingUrl", t.streamingUrl},
            {"isAvailable", t.isAvailable},
            {"externalUrl", t.externalUrl},
            {"durationMs", t.durationMs},
            {"isExplicit", t.isExplicit},
            {"discNumber", t.discNumber},
            {"trackNumber", t.trackNumber}
        };
    }

    friend void from_json(const nlohmann::json& j, StreamingTrack& t) {
        if (j.contains("localTrackId")) j.at("localTrackId").get_to(t.localTrackId);
        if (j.contains("source")) j.at("source").get_to(t.source);
        if (j.contains("externalId")) j.at("externalId").get_to(t.externalId);
        if (j.contains("externalPath")) j.at("externalPath").get_to(t.externalPath);
        if (j.contains("syncedAt")) j.at("syncedAt").get_to(t.syncedAt);
        if (j.contains("serviceType")) j.at("serviceType").get_to(t.serviceType);
        if (j.contains("serviceId")) j.at("serviceId").get_to(t.serviceId);
        if (j.contains("previewUrl")) j.at("previewUrl").get_to(t.previewUrl);
        if (j.contains("artworkUrl")) j.at("artworkUrl").get_to(t.artworkUrl);
        if (j.contains("isrc")) j.at("isrc").get_to(t.isrc);
        if (j.contains("popularity")) j.at("popularity").get_to(t.popularity);
        if (j.contains("streamingUrl")) j.at("streamingUrl").get_to(t.streamingUrl);
        if (j.contains("isAvailable")) j.at("isAvailable").get_to(t.isAvailable);
        if (j.contains("externalUrl")) j.at("externalUrl").get_to(t.externalUrl);
        if (j.contains("durationMs")) j.at("durationMs").get_to(t.durationMs);
        if (j.contains("isExplicit")) j.at("isExplicit").get_to(t.isExplicit);
        if (j.contains("discNumber")) j.at("discNumber").get_to(t.discNumber);
        if (j.contains("trackNumber")) j.at("trackNumber").get_to(t.trackNumber);
    }
};

} // namespace BeatMate::Models
