#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

enum class PlaylistSource : int {
    Local       = 0,
    Rekordbox   = 1,
    VirtualDJ   = 2,
    Serato      = 3,
    Traktor     = 4,
    EngineDJ    = 5
};

NLOHMANN_JSON_SERIALIZE_ENUM(PlaylistSource, {
    { PlaylistSource::Local,      "Local" },
    { PlaylistSource::Rekordbox,  "Rekordbox" },
    { PlaylistSource::VirtualDJ,  "VirtualDJ" },
    { PlaylistSource::Serato,     "Serato" },
    { PlaylistSource::Traktor,    "Traktor" },
    { PlaylistSource::EngineDJ,   "EngineDJ" }
})

enum class PlaylistSortOrder : int {
    Manual = 0,
    Title = 1,
    Artist = 2,
    Album = 3,
    BPM = 4,
    Key = 5,
    Rating = 6,
    DateAdded = 7,
    Duration = 8,
    Genre = 9,
    Energy = 10,
    Year = 11,
    PlayCount = 12
};

NLOHMANN_JSON_SERIALIZE_ENUM(PlaylistSortOrder, {
    { PlaylistSortOrder::Manual, "Manual" },
    { PlaylistSortOrder::Title, "Title" },
    { PlaylistSortOrder::Artist, "Artist" },
    { PlaylistSortOrder::Album, "Album" },
    { PlaylistSortOrder::BPM, "BPM" },
    { PlaylistSortOrder::Key, "Key" },
    { PlaylistSortOrder::Rating, "Rating" },
    { PlaylistSortOrder::DateAdded, "DateAdded" },
    { PlaylistSortOrder::Duration, "Duration" },
    { PlaylistSortOrder::Genre, "Genre" },
    { PlaylistSortOrder::Energy, "Energy" },
    { PlaylistSortOrder::Year, "Year" },
    { PlaylistSortOrder::PlayCount, "PlayCount" }
})

enum class SortDirection : int {
    Ascending = 0,
    Descending = 1
};

NLOHMANN_JSON_SERIALIZE_ENUM(SortDirection, {
    { SortDirection::Ascending, "Ascending" },
    { SortDirection::Descending, "Descending" }
})

struct Playlist {
    int64_t id = 0;
    std::string name;
    std::string description;

    std::vector<int64_t> trackIds;

    bool isSmartPlaylist = false;

    int64_t createdAt = 0;      // unix timestamp
    int64_t modifiedAt = 0;     // unix timestamp

    std::string color;          // hex string
    std::string icon;           // icon name or path

    int64_t parentFolderId = -1; // -1 = root level

    PlaylistSortOrder sortOrder = PlaylistSortOrder::Manual;
    SortDirection sortDirection = SortDirection::Ascending;

    PlaylistSource source = PlaylistSource::Local;
    std::string externalId;       // UUID / path / name cote DJ app
    std::string externalPath;     // fichier d'origine (.crate, .nml, .m3u, etc.)

    Playlist() = default;

    Playlist(int64_t id, const std::string& name)
        : id(id), name(name) {}

    bool operator==(const Playlist& other) const { return id == other.id; }
    bool operator!=(const Playlist& other) const { return id != other.id; }

    [[nodiscard]] size_t trackCount() const { return trackIds.size(); }
    [[nodiscard]] bool isEmpty() const { return trackIds.empty(); }
    [[nodiscard]] bool isFolder() const { return trackIds.empty() && !isSmartPlaylist; }

    void addTrack(int64_t trackId) { trackIds.push_back(trackId); }

    void removeTrack(int64_t trackId) {
        trackIds.erase(
            std::remove(trackIds.begin(), trackIds.end(), trackId),
            trackIds.end()
        );
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Playlist,
        id, name, description, trackIds,
        isSmartPlaylist, createdAt, modifiedAt,
        color, icon, parentFolderId,
        sortOrder, sortDirection,
        source, externalId, externalPath
    )
};

} // namespace BeatMate::Models
