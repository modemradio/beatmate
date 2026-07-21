#pragma once

#include <string>
#include <vector>

#include "../../../models/PioneerTrack.h"
#include "../../../models/Playlist.h"

namespace BeatMate::Services::PioneerDJ {

class PioneerXmlParser {
public:
    PioneerXmlParser() = default;
    ~PioneerXmlParser() = default;

    bool parseXml(const std::string& path);

    std::vector<Models::PioneerTrack> getTracks() const { return tracks_; }
    std::vector<Models::Playlist> getPlaylists() const { return playlists_; }

    std::string getLibraryName() const { return libraryName_; }
    std::string getVersion() const { return version_; }

private:
    void parseTrackNode(const std::string& xmlContent, size_t startPos);
    void parsePlaylistNode(const std::string& xmlContent, size_t startPos);
    std::string getAttributeValue(const std::string& xml, size_t startPos, const std::string& attrName);

    std::vector<Models::PioneerTrack> tracks_;
    std::vector<Models::Playlist> playlists_;
    std::string libraryName_;
    std::string version_;
};

}
