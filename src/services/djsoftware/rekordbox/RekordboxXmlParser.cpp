#include "RekordboxXmlParser.h"

#include <spdlog/spdlog.h>
#include <juce_core/juce_core.h>

#include <fstream>
#include <sstream>
#include <filesystem>

namespace BeatMate::Services::Rekordbox {

bool RekordboxXmlParser::parseXml(const std::string& path) {
    juce::File xmlFile{juce::String(path)};
    if (!xmlFile.existsAsFile()) {
        spdlog::error("RekordboxXmlParser: Cannot open XML file: {}", path);
        return false;
    }

    auto xmlDoc = juce::XmlDocument::parse(xmlFile);
    if (xmlDoc == nullptr) {
        spdlog::error("RekordboxXmlParser: XML parse error in file: {}", path);
        return false;
    }

    tracks_.clear();
    playlists_.clear();

    if (xmlDoc->getTagName() == "DJ_PLAYLISTS") {
        if (xmlDoc->hasAttribute("Version")) {
            version_ = xmlDoc->getStringAttribute("Version").toStdString();
        }
    }

    if (auto* productElem = xmlDoc->getChildByName("PRODUCT")) {
        if (productElem->hasAttribute("Name")) {
            libraryName_ = productElem->getStringAttribute("Name").toStdString();
        }
    }

    auto* collectionElem = xmlDoc->getChildByName("COLLECTION");
    if (collectionElem == nullptr) {
        collectionElem = xmlDoc.get();
    }

    for (auto* trackElem = collectionElem->getChildByName("TRACK");
         trackElem != nullptr;
         trackElem = trackElem->getNextElementWithTagName("TRACK")) {

        Models::RekordboxTrack track;

        if (trackElem->hasAttribute("TrackID")) {
            track.rekordboxId = trackElem->getStringAttribute("TrackID").toStdString();
            track.externalId = track.rekordboxId;
        }
        if (trackElem->hasAttribute("Location")) {
            auto loc = trackElem->getStringAttribute("Location").toStdString();
            if (loc.find("file://localhost/") == 0)
                loc = loc.substr(16);
            else if (loc.find("file:///") == 0)
                loc = loc.substr(8);
            track.externalPath = juce::URL::removeEscapeChars(juce::String(loc)).toStdString();
        }
        if (trackElem->hasAttribute("Name"))
            track.title = trackElem->getStringAttribute("Name").toStdString();
        if (trackElem->hasAttribute("Artist"))
            track.artist = trackElem->getStringAttribute("Artist").toStdString();
        if (trackElem->hasAttribute("Album"))
            track.album = trackElem->getStringAttribute("Album").toStdString();
        if (trackElem->hasAttribute("Genre"))
            track.genre = trackElem->getStringAttribute("Genre").toStdString();
        if (trackElem->hasAttribute("AverageBpm"))
            track.bpm = trackElem->getDoubleAttribute("AverageBpm");
        if (trackElem->hasAttribute("TotalTime"))
            track.duration = trackElem->getDoubleAttribute("TotalTime");
        if (trackElem->hasAttribute("Year"))
            track.year = trackElem->getIntAttribute("Year");
        if (trackElem->hasAttribute("Label"))
            track.label = trackElem->getStringAttribute("Label").toStdString();
        if (trackElem->hasAttribute("Tonality")) {
            track.tonality = trackElem->getStringAttribute("Tonality").toStdString();
        }
        if (trackElem->hasAttribute("Rating")) {
            track.rating = trackElem->getIntAttribute("Rating");
        }
        if (trackElem->hasAttribute("Colour")) {
            track.color = trackElem->getStringAttribute("Colour").toStdString();
        }
        if (trackElem->hasAttribute("Comments")) {
            track.comment = trackElem->getStringAttribute("Comments").toStdString();
        }
        if (trackElem->hasAttribute("DateAdded")) {
            track.dateAdded = trackElem->getStringAttribute("DateAdded").toStdString();
        }
        if (trackElem->hasAttribute("Mix")) {
            track.mixName = trackElem->getStringAttribute("Mix").toStdString();
        }
        if (trackElem->hasAttribute("Remixer")) {
            track.remixer = trackElem->getStringAttribute("Remixer").toStdString();
        }

        for (auto* cueElem = trackElem->getChildByName("POSITION_MARK");
             cueElem != nullptr;
             cueElem = cueElem->getNextElementWithTagName("POSITION_MARK")) {

            Models::RekordboxCue cue;
            if (cueElem->hasAttribute("Num")) cue.number = cueElem->getIntAttribute("Num");
            if (cueElem->hasAttribute("Start")) cue.position = cueElem->getDoubleAttribute("Start");
            if (cueElem->hasAttribute("End")) {
                double end = cueElem->getDoubleAttribute("End");
                if (end > cue.position) {
                    cue.length = end - cue.position;
                    cue.isLoop = true;
                }
            }
            if (cueElem->hasAttribute("Name")) cue.name = cueElem->getStringAttribute("Name").toStdString();
            track.hotCues.push_back(cue);
        }

        track.source = Models::TrackSource::Rekordbox;
        tracks_.push_back(track);
    }

    std::function<void(juce::XmlElement*, int64_t)> parseNodes =
        [&](juce::XmlElement* parent, int64_t parentFolderId) {
        for (auto* nodeElem = parent->getChildByName("NODE");
             nodeElem != nullptr;
             nodeElem = nodeElem->getNextElementWithTagName("NODE")) {

            int nodeType = nodeElem->hasAttribute("Type") ? nodeElem->getIntAttribute("Type") : -1;
            std::string nodeName = nodeElem->hasAttribute("Name")
                ? nodeElem->getStringAttribute("Name").toStdString() : std::string{};

            if (nodeType == 0) {
                Models::Playlist folder;
                folder.name = nodeName;
                folder.parentFolderId = parentFolderId;
                int64_t synthId = -(static_cast<int64_t>(playlists_.size()) + 2);
                folder.id = synthId;
                playlists_.push_back(folder);
                parseNodes(nodeElem, synthId);
            }
            else if (nodeType == 1) {
                Models::Playlist pl;
                pl.name = nodeName;
                pl.parentFolderId = parentFolderId;
                for (auto* t = nodeElem->getChildByName("TRACK");
                     t != nullptr;
                     t = t->getNextElementWithTagName("TRACK")) {
                    if (t->hasAttribute("Key")) {
                        auto k = t->getStringAttribute("Key");
                        auto idVal = k.getLargeIntValue();
                        if (idVal > 0)
                            pl.trackIds.push_back(static_cast<int64_t>(idVal));
                    }
                }
                playlists_.push_back(pl);
            }
            else {
                parseNodes(nodeElem, parentFolderId);
            }
        }
    };

    if (auto* playlistsElem = xmlDoc->getChildByName("PLAYLISTS")) {
        parseNodes(playlistsElem, -1);
    }

    spdlog::info("RekordboxXmlParser: Parsed {} tracks and {} playlists from {}",
                 tracks_.size(), playlists_.size(), path);
    return true;
}

void RekordboxXmlParser::parseTrackNode(const std::string&, size_t) {
}

void RekordboxXmlParser::parsePlaylistNode(const std::string&, size_t) {
}

std::string RekordboxXmlParser::getAttributeValue(const std::string& xml, size_t startPos, const std::string& attrName) {
    std::string search = attrName + "=\"";
    auto pos = xml.find(search, startPos);
    if (pos == std::string::npos) return "";
    pos += search.length();
    auto endPos = xml.find('"', pos);
    if (endPos == std::string::npos) return "";
    return xml.substr(pos, endPos - pos);
}

}
