#include "TraktorCollectionParser.h"

#include <spdlog/spdlog.h>
#include <juce_core/juce_core.h>

namespace BeatMate::Services::Traktor {

std::vector<Models::TraktorTrack> TraktorCollectionParser::parse(const std::string& nmlPath) {
    std::vector<Models::TraktorTrack> tracks;

    juce::File nmlFile{juce::String(nmlPath)};
    if (!nmlFile.existsAsFile()) {
        spdlog::error("TraktorCollectionParser: Cannot open {}", nmlPath);
        return tracks;
    }

    auto xmlDoc = juce::XmlDocument::parse(nmlFile);
    if (xmlDoc == nullptr) {
        spdlog::error("TraktorCollectionParser: XML parse error in {}", nmlPath);
        return tracks;
    }

    if (xmlDoc->getTagName() == "NML") {
        if (xmlDoc->hasAttribute("VERSION")) {
            version_ = xmlDoc->getIntAttribute("VERSION");
        }
    }

    auto* collectionElem = xmlDoc->getChildByName("COLLECTION");
    if (collectionElem == nullptr) {
        collectionElem = xmlDoc.get();
    }

    for (auto* entryElem = collectionElem->getChildByName("ENTRY");
         entryElem != nullptr;
         entryElem = entryElem->getNextElementWithTagName("ENTRY")) {

        Models::TraktorTrack currentTrack;

        if (entryElem->hasAttribute("AUDIO_ID")) {
            currentTrack.traktorId = entryElem->getStringAttribute("AUDIO_ID").toStdString();
            currentTrack.externalId = currentTrack.traktorId;
        }
        currentTrack.title  = entryElem->getStringAttribute("TITLE").toStdString();
        currentTrack.artist = entryElem->getStringAttribute("ARTIST").toStdString();
        if (auto* alb = entryElem->getChildByName("ALBUM")) {
            currentTrack.album = alb->getStringAttribute("TITLE").toStdString();
        }

        if (auto* locElem = entryElem->getChildByName("LOCATION")) {
            if (locElem->hasAttribute("DIR")) {
                currentTrack.directory = locElem->getStringAttribute("DIR").toStdString();
            }
            if (locElem->hasAttribute("FILE")) {
                currentTrack.filename = locElem->getStringAttribute("FILE").toStdString();
            }
            if (locElem->hasAttribute("VOLUME")) {
                currentTrack.volume = locElem->getStringAttribute("VOLUME").toStdString();
            }
            std::string cleanedDir = currentTrack.directory;
            size_t pos = 0;
            while ((pos = cleanedDir.find("/:", pos)) != std::string::npos) {
                cleanedDir.replace(pos, 2, "/");
                pos += 1;
            }
            currentTrack.externalPath = currentTrack.volume + cleanedDir + currentTrack.filename;
        }

        if (auto* tempoElem = entryElem->getChildByName("TEMPO")) {
            if (tempoElem->hasAttribute("BPM")) {
                currentTrack.traktorBpm = tempoElem->getDoubleAttribute("BPM");
            }
            if (tempoElem->hasAttribute("BPM_QUALITY")) {
                currentTrack.traktorBpmQuality = tempoElem->getDoubleAttribute("BPM_QUALITY");
            }
        }

        if (auto* keyElem = entryElem->getChildByName("MUSICAL_KEY")) {
            if (keyElem->hasAttribute("VALUE")) {
                currentTrack.musicalKey = keyElem->getStringAttribute("VALUE").toStdString();
            }
        }

        if (auto* infoElem = entryElem->getChildByName("INFO")) {
            if (infoElem->hasAttribute("PLAYCOUNT")) {
                currentTrack.traktorPlayCount = infoElem->getIntAttribute("PLAYCOUNT");
            }
            if (infoElem->hasAttribute("RATING")) {
                currentTrack.traktorRating = infoElem->getIntAttribute("RATING");
            }
            if (infoElem->hasAttribute("IMPORT_DATE")) {
                currentTrack.importDate = infoElem->getStringAttribute("IMPORT_DATE").toStdString();
            }
            if (infoElem->hasAttribute("LAST_PLAYED")) {
                currentTrack.lastPlayDate = infoElem->getStringAttribute("LAST_PLAYED").toStdString();
            }
            if (infoElem->hasAttribute("COLOR")) {
                currentTrack.traktorColor = infoElem->getStringAttribute("COLOR").toStdString();
            }
            if (infoElem->hasAttribute("GENRE")) {
                currentTrack.genre = infoElem->getStringAttribute("GENRE").toStdString();
            }
            if (infoElem->hasAttribute("LABEL")) {
                currentTrack.label = infoElem->getStringAttribute("LABEL").toStdString();
            }
            if (infoElem->hasAttribute("COMMENT")) {
                currentTrack.comment = infoElem->getStringAttribute("COMMENT").toStdString();
            }
            if (infoElem->hasAttribute("RELEASE_DATE")) {
                auto rel = infoElem->getStringAttribute("RELEASE_DATE");
                if (rel.isNotEmpty()) currentTrack.year = rel.substring(0, 4).getIntValue();
            }
            if (infoElem->hasAttribute("PLAYTIME")) {
                currentTrack.durationSec = infoElem->getDoubleAttribute("PLAYTIME");
            }
        }

        for (auto* cueElem = entryElem->getChildByName("CUE_V2");
             cueElem != nullptr;
             cueElem = cueElem->getNextElementWithTagName("CUE_V2")) {

            Models::TraktorTrack::TraktorCue cue;
            if (cueElem->hasAttribute("TYPE")) cue.type = cueElem->getIntAttribute("TYPE");
            if (cueElem->hasAttribute("START")) cue.start = cueElem->getDoubleAttribute("START") / 1000.0;
            if (cueElem->hasAttribute("LEN")) cue.length = cueElem->getDoubleAttribute("LEN") / 1000.0;
            if (cueElem->hasAttribute("HOTCUE")) cue.hotcue = cueElem->getIntAttribute("HOTCUE");
            if (cueElem->hasAttribute("NAME")) cue.name = cueElem->getStringAttribute("NAME").toStdString();
            currentTrack.traktorCues.push_back(cue);
        }

        if (auto* loudElem = entryElem->getChildByName("LOUDNESS")) {
            if (loudElem->hasAttribute("PERCEIVED_DB")) {
                currentTrack.lockGain = static_cast<float>(loudElem->getDoubleAttribute("PERCEIVED_DB"));
            }
        }

        currentTrack.source = Models::TrackSource::Traktor;
        tracks.push_back(currentTrack);
    }

    spdlog::info("TraktorCollectionParser: Parsed {} tracks from {}", tracks.size(), nmlPath);
    return tracks;
}

static void walkPlaylistNodes(const juce::XmlElement* node,
                              const std::string& parentPath,
                              std::vector<TraktorPlaylistInfo>& out)
{
    if (!node) return;
    for (auto* child = node->getChildByName("NODE");
         child != nullptr;
         child = child->getNextElementWithTagName("NODE"))
    {
        const juce::String type = child->getStringAttribute("TYPE");
        const juce::String name = child->getStringAttribute("NAME");
        const std::string fullPath = parentPath + "/" + name.toStdString();

        TraktorPlaylistInfo info;
        info.name = name.toStdString();
        info.parentPath = parentPath;
        info.fullPath = fullPath;
        info.isFolder = (type == "FOLDER");

        if (type == "PLAYLIST") {
            // Children: <PLAYLIST><ENTRY><PRIMARYKEY TYPE="TRACK" KEY="VOLUME/DIR/FILE"/></ENTRY>...</PLAYLIST>
            if (auto* pl = child->getChildByName("PLAYLIST")) {
                int pos = 0;
                for (auto* entry = pl->getChildByName("ENTRY");
                     entry != nullptr;
                     entry = entry->getNextElementWithTagName("ENTRY"))
                {
                    if (auto* pk = entry->getChildByName("PRIMARYKEY")) {
                        const juce::String key = pk->getStringAttribute("KEY");
                        if (key.isNotEmpty()) {
                            TraktorPlaylistTrackRef ref;
                            ref.trackPath = key.toStdString();
                            ref.position = pos++;
                            info.entries.push_back(std::move(ref));
                        }
                    }
                }
            }
        }

        out.push_back(std::move(info));

        if (type == "FOLDER") {
            if (auto* sub = child->getChildByName("SUBNODES"))
                walkPlaylistNodes(sub, fullPath, out);
            else
                walkPlaylistNodes(child, fullPath, out);
        }
    }
}

std::vector<TraktorPlaylistInfo>
TraktorCollectionParser::parsePlaylists(const std::string& nmlPath)
{
    std::vector<TraktorPlaylistInfo> out;
    juce::File nmlFile{juce::String(nmlPath)};
    if (!nmlFile.existsAsFile()) return out;

    auto xmlDoc = juce::XmlDocument::parse(nmlFile);
    if (xmlDoc == nullptr) return out;

    auto* playlistsElem = xmlDoc->getChildByName("PLAYLISTS");
    if (!playlistsElem) return out;

    walkPlaylistNodes(playlistsElem, "", out);
    spdlog::info("TraktorCollectionParser: Parsed {} playlist nodes from {}",
                 out.size(), nmlPath);
    return out;
}

} // namespace BeatMate::Services::Traktor
