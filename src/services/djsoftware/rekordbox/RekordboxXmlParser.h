#pragma once

#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "../../../models/RekordboxTrack.h"
#include "../../../models/Playlist.h"

namespace BeatMate::Services::Rekordbox {

class RekordboxXmlParser {
public:
    RekordboxXmlParser() = default;
    ~RekordboxXmlParser() = default;

    bool parseXml(const std::string& path);

    bool parse(const juce::File& xmlFile) {
        return parseXml(xmlFile.getFullPathName().toStdString());
    }

    std::vector<Models::RekordboxTrack> getTracks() const { return tracks_; }
    std::vector<Models::Playlist> getPlaylists() const { return playlists_; }

    std::string getLibraryName() const { return libraryName_; }
    std::string getVersion() const { return version_; }

private:
    void parseTrackNode(const std::string& xmlContent, size_t startPos);
    void parsePlaylistNode(const std::string& xmlContent, size_t startPos);
    std::string getAttributeValue(const std::string& xml, size_t startPos, const std::string& attrName);

    std::vector<Models::RekordboxTrack> tracks_;
    std::vector<Models::Playlist> playlists_;
    std::string libraryName_;
    std::string version_;
};

} // namespace BeatMate::Services::Rekordbox
