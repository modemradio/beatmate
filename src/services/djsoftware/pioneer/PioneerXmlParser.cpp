#include "PioneerXmlParser.h"

#include <spdlog/spdlog.h>
#include <juce_core/juce_core.h>

#include <fstream>
#include <sstream>
#include <filesystem>

namespace BeatMate::Services::PioneerDJ {

bool PioneerXmlParser::parseXml(const std::string& path) {
    juce::File xmlFile{juce::String(path)};
    if (!xmlFile.existsAsFile()) {
        spdlog::error("PioneerXmlParser: Cannot open XML file: {}", path);
        return false;
    }

    auto xmlDoc = juce::XmlDocument::parse(xmlFile);
    if (xmlDoc == nullptr) {
        spdlog::error("PioneerXmlParser: XML parse error in file: {}", path);
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

        Models::PioneerTrack track;

        if (trackElem->hasAttribute("TrackID")) {
            track.rekordboxId = trackElem->getStringAttribute("TrackID").toStdString();
            track.externalId = track.rekordboxId;
        }
        if (trackElem->hasAttribute("Location")) {
            track.externalPath = trackElem->getStringAttribute("Location").toStdString();
        }
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

            Models::PioneerDJCue cue;
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

    std::function<void(juce::XmlElement*)> parseNodes = [&](juce::XmlElement* parent) {
        for (auto* nodeElem = parent->getChildByName("NODE");
             nodeElem != nullptr;
             nodeElem = nodeElem->getNextElementWithTagName("NODE")) {

            if (nodeElem->hasAttribute("Type") && nodeElem->getIntAttribute("Type") == 1) {
                Models::Playlist pl;
                if (nodeElem->hasAttribute("Name")) {
                    pl.name = nodeElem->getStringAttribute("Name").toStdString();
                }
                playlists_.push_back(pl);
            }
            parseNodes(nodeElem);
        }
    };

    if (auto* playlistsElem = xmlDoc->getChildByName("PLAYLISTS")) {
        parseNodes(playlistsElem);
    }

    spdlog::info("PioneerXmlParser: Parsed {} tracks and {} playlists from {}",
                 tracks_.size(), playlists_.size(), path);
    return true;
}

void PioneerXmlParser::parseTrackNode(const std::string&, size_t) {
    // Handled via juce::XmlDocument parser above
}

void PioneerXmlParser::parsePlaylistNode(const std::string&, size_t) {
    // Handled via juce::XmlDocument parser above
}

std::string PioneerXmlParser::getAttributeValue(const std::string& xml, size_t startPos, const std::string& attrName) {
    std::string search = attrName + "=\"";
    auto pos = xml.find(search, startPos);
    if (pos == std::string::npos) return "";
    pos += search.length();
    auto endPos = xml.find('"', pos);
    if (endPos == std::string::npos) return "";
    return xml.substr(pos, endPos - pos);
}

} // namespace BeatMate::Services::PioneerDJ
