#pragma once

#include <string>
#include <vector>

#include "../../../models/TraktorTrack.h"

namespace BeatMate::Services::Traktor {

struct TraktorPlaylistTrackRef {
    std::string trackPath;   // full path assembled from VOLUME+DIR+FILE
    int position = 0;
};

struct TraktorPlaylistInfo {
    std::string fullPath;                           // "/Root/Folder/Name"
    std::string name;
    std::string parentPath;
    bool isFolder = false;
    std::vector<TraktorPlaylistTrackRef> entries;   // only for playlists
};

class TraktorCollectionParser {
public:
    TraktorCollectionParser() = default;
    ~TraktorCollectionParser() = default;

    std::vector<Models::TraktorTrack> parse(const std::string& nmlPath);

    std::vector<TraktorPlaylistInfo> parsePlaylists(const std::string& nmlPath);

    std::string getCollectionName() const { return collectionName_; }
    int getTraktorVersion() const { return version_; }

private:
    std::string collectionName_;
    int version_ = 0;
};

} // namespace BeatMate::Services::Traktor
