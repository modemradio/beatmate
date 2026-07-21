#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

struct PlaylistTrack {
    int64_t playlistId = 0;
    int64_t trackId = 0;
    int position = 0;          // position within the playlist (0-based)
    int64_t addedAt = 0;       // unix timestamp

    PlaylistTrack() = default;

    PlaylistTrack(int64_t playlistId, int64_t trackId, int position)
        : playlistId(playlistId), trackId(trackId), position(position) {}

    PlaylistTrack(int64_t playlistId, int64_t trackId, int position, int64_t addedAt)
        : playlistId(playlistId), trackId(trackId), position(position), addedAt(addedAt) {}

    bool operator==(const PlaylistTrack& other) const {
        return playlistId == other.playlistId && trackId == other.trackId;
    }

    bool operator<(const PlaylistTrack& other) const {
        if (playlistId != other.playlistId) return playlistId < other.playlistId;
        return position < other.position;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PlaylistTrack,
        playlistId, trackId, position, addedAt
    )
};

} // namespace BeatMate::Models
