#pragma once

#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>
#include "Track.h"

namespace BeatMate::Models {

struct DJSoftwareTrack {
    int64_t localTrackId = 0;
    TrackSource source = TrackSource::Local;
    std::string externalId;
    std::string externalPath;
    int64_t syncedAt = 0;

    DJSoftwareTrack() = default;

    DJSoftwareTrack(int64_t localTrackId, TrackSource source, const std::string& externalId)
        : localTrackId(localTrackId), source(source), externalId(externalId) {}

    bool operator==(const DJSoftwareTrack& other) const {
        return localTrackId == other.localTrackId && source == other.source;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(DJSoftwareTrack,
        localTrackId, source, externalId, externalPath, syncedAt
    )
};

}
