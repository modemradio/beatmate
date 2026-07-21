#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include "DJSoftwareTrack.h"

namespace BeatMate::Models {

struct PlatformVDJAutomix {
    std::string type;
    double fadeIn = 0.0;        // seconds
    double fadeOut = 0.0;       // seconds
    double startPosition = 0.0;
    double endPosition = 0.0;

    PlatformVDJAutomix() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PlatformVDJAutomix,
        type, fadeIn, fadeOut, startPosition, endPosition
    )
};

struct PlatformVDJScanData {
    std::string bpmScan;
    std::string gainScan;
    std::string waveformScan;
    bool scanComplete = false;

    PlatformVDJScanData() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PlatformVDJScanData,
        bpmScan, gainScan, waveformScan, scanComplete
    )
};

struct PlatformVDJTrack : public DJSoftwareTrack {
    std::string vdjId;
    std::map<std::string, std::string> tags;
    PlatformVDJAutomix automix;
    PlatformVDJScanData scanData;

    std::string infos;
    std::string firstSeen;
    std::string lastPlay;
    int playCount = 0;
    double gain = 0.0;          // auto-gain value (dB)
    std::string flag;
    std::string fileFolder;

    std::vector<std::string> pois;

    PlatformVDJTrack() {
        source = TrackSource::VirtualDJ;
    }

    explicit PlatformVDJTrack(const std::string& vdjId)
        : vdjId(vdjId) {
        source = TrackSource::VirtualDJ;
    }

    friend void to_json(nlohmann::json& j, const PlatformVDJTrack& t) {
        j = nlohmann::json{
            {"localTrackId", t.localTrackId},
            {"source", t.source},
            {"externalId", t.externalId},
            {"externalPath", t.externalPath},
            {"syncedAt", t.syncedAt},
            {"vdjId", t.vdjId},
            {"tags", t.tags},
            {"automix", t.automix},
            {"scanData", t.scanData},
            {"infos", t.infos},
            {"firstSeen", t.firstSeen},
            {"lastPlay", t.lastPlay},
            {"playCount", t.playCount},
            {"gain", t.gain},
            {"flag", t.flag},
            {"fileFolder", t.fileFolder},
            {"pois", t.pois}
        };
    }

    friend void from_json(const nlohmann::json& j, PlatformVDJTrack& t) {
        if (j.contains("localTrackId")) j.at("localTrackId").get_to(t.localTrackId);
        if (j.contains("source")) j.at("source").get_to(t.source);
        if (j.contains("externalId")) j.at("externalId").get_to(t.externalId);
        if (j.contains("externalPath")) j.at("externalPath").get_to(t.externalPath);
        if (j.contains("syncedAt")) j.at("syncedAt").get_to(t.syncedAt);
        if (j.contains("vdjId")) j.at("vdjId").get_to(t.vdjId);
        if (j.contains("tags")) j.at("tags").get_to(t.tags);
        if (j.contains("automix")) j.at("automix").get_to(t.automix);
        if (j.contains("scanData")) j.at("scanData").get_to(t.scanData);
        if (j.contains("infos")) j.at("infos").get_to(t.infos);
        if (j.contains("firstSeen")) j.at("firstSeen").get_to(t.firstSeen);
        if (j.contains("lastPlay")) j.at("lastPlay").get_to(t.lastPlay);
        if (j.contains("playCount")) j.at("playCount").get_to(t.playCount);
        if (j.contains("gain")) j.at("gain").get_to(t.gain);
        if (j.contains("flag")) j.at("flag").get_to(t.flag);
        if (j.contains("fileFolder")) j.at("fileFolder").get_to(t.fileFolder);
        if (j.contains("pois")) j.at("pois").get_to(t.pois);
    }
};

} // namespace BeatMate::Models
