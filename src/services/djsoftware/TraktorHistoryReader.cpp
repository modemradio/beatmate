#include "TraktorHistoryReader.h"

#include <juce_core/juce_core.h>
#include <spdlog/spdlog.h>

#include <algorithm>

namespace BeatMate::Services::DJSoftware {

namespace {

static std::vector<juce::File> findTraktorHistoryNmls()
{
    std::vector<juce::File> result;
    auto documents = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    auto niRoot = documents.getChildFile("Native Instruments");
    if (!niRoot.isDirectory()) return result;

    juce::Array<juce::File> traktorDirs;
    niRoot.findChildFiles(traktorDirs, juce::File::findDirectories, false, "Traktor*");
    for (auto& tdir : traktorDirs) {
        auto hist = tdir.getChildFile("History");
        if (!hist.isDirectory()) continue;
        juce::Array<juce::File> nmls;
        hist.findChildFiles(nmls, juce::File::findFiles, false, "history_*.nml");
        for (auto& f : nmls) result.push_back(f);
    }
    std::sort(result.begin(), result.end(),
        [](const juce::File& a, const juce::File& b) {
            return a.getLastModificationTime() > b.getLastModificationTime();
        });
    return result;
}

static std::string locationToPath(juce::XmlElement* location)
{
    if (!location) return {};
    juce::String volume = location->getStringAttribute("VOLUME");
    juce::String dir    = location->getStringAttribute("DIR");
    juce::String file   = location->getStringAttribute("FILE");

    dir = dir.replace("/:", "/");
    juce::String full = volume + dir + file;
    return full.toStdString();
}

}

std::vector<PlayedTrack> TraktorHistoryReader::readRecentHistory(int maxTracks)
{
    std::vector<PlayedTrack> result;

    try {
        auto files = findTraktorHistoryNmls();
        if (files.empty()) {
            spdlog::debug("[TraktorHistoryReader] no history_*.nml files found");
            return {};
        }

        for (const auto& f : files) {
            if ((int) result.size() >= maxTracks) break;

            auto xmlDoc = juce::XmlDocument::parse(f);
            if (!xmlDoc) {
                spdlog::warn("[TraktorHistoryReader] failed to parse {}",
                             f.getFullPathName().toStdString());
                continue;
            }
            auto* collection = xmlDoc->getChildByName("COLLECTION");
            if (!collection) continue;

            const int64_t fileTimeUnix =
                f.getLastModificationTime().toMilliseconds() / 1000;

            for (auto* entry : collection->getChildWithTagNameIterator("ENTRY")) {
                if ((int) result.size() >= maxTracks) break;

                PlayedTrack pt;
                pt.title  = entry->getStringAttribute("TITLE").toStdString();
                pt.artist = entry->getStringAttribute("ARTIST").toStdString();

                if (auto* loc = entry->getChildByName("LOCATION"))
                    pt.filePath = locationToPath(loc);
                if (auto* album = entry->getChildByName("ALBUM"))
                    pt.album = album->getStringAttribute("TITLE").toStdString();
                if (auto* info = entry->getChildByName("INFO")) {
                    pt.genre = info->getStringAttribute("GENRE").toStdString();
                    auto playtime = info->getDoubleAttribute("PLAYTIME", 0.0);
                    if (playtime > 0.0) pt.durationSec = playtime;
                    int rating = info->getIntAttribute("RANKING", 0);
                    pt.rating = rating > 0 ? (rating / 51) : 0;
                }
                if (auto* tempo = entry->getChildByName("TEMPO"))
                    pt.bpm = tempo->getDoubleAttribute("BPM", 0.0);

                juce::String startDate = entry->getStringAttribute("STARTDATE");
                if (startDate.isNotEmpty()) {
                    auto parts = juce::StringArray::fromTokens(startDate, "/", "");
                    if (parts.size() == 3) {
                        juce::Time t(parts[0].getIntValue(),
                                     juce::jmax(0, parts[1].getIntValue() - 1),
                                     parts[2].getIntValue(), 0, 0, 0, 0);
                        if (t.toMilliseconds() > 0)
                            pt.playedAtUnix = t.toMilliseconds() / 1000;
                    }
                }
                if (pt.playedAtUnix == 0) pt.playedAtUnix = fileTimeUnix;
                pt.source = "Traktor";

                if (!pt.title.empty() || !pt.filePath.empty())
                    result.push_back(std::move(pt));
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("[TraktorHistoryReader] failed: {}", e.what());
        return {};
    } catch (...) {
        spdlog::warn("[TraktorHistoryReader] failed (unknown)");
        return {};
    }
    return result;
}

}
