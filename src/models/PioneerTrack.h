#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>
#include "DJSoftwareTrack.h"
#include "CuePoint.h"

namespace BeatMate::Models {

struct PioneerDJCue {
    int number = 0;             // hot cue number (0-7)
    double position = 0.0;      // seconds
    double length = 0.0;        // seconds (for loops)
    std::string name;
    std::string color;          // hex color
    bool isLoop = false;

    PioneerDJCue() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PioneerDJCue,
        number, position, length, name, color, isLoop
    )
};

struct PioneerDJMemoryCue {
    double position = 0.0;
    std::string name;
    std::string color;

    PioneerDJMemoryCue() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PioneerDJMemoryCue,
        position, name, color
    )
};

struct PioneerTrack : public DJSoftwareTrack {
    std::string rekordboxId;
    std::vector<std::string> playlistNames;

    std::vector<PioneerDJCue> hotCues;
    std::vector<PioneerDJMemoryCue> memCues;

    bool beatGridLocked = false;
    std::string color;
    int rating = 0;                 // 0-5
    std::string comment;
    std::string comment2;           // PioneerDJ has 2 comment fields

    std::string tonality;           // key as displayed in PioneerDJ
    std::string mixName;
    std::string remixer;
    std::string dateAdded;

    PioneerTrack() {
        source = TrackSource::Rekordbox;
    }

    explicit PioneerTrack(const std::string& rekordboxId)
        : rekordboxId(rekordboxId) {
        source = TrackSource::Rekordbox;
    }

    friend void to_json(nlohmann::json& j, const PioneerTrack& t) {
        j = nlohmann::json{
            {"localTrackId", t.localTrackId},
            {"source", t.source},
            {"externalId", t.externalId},
            {"externalPath", t.externalPath},
            {"syncedAt", t.syncedAt},
            {"rekordboxId", t.rekordboxId},
            {"playlistNames", t.playlistNames},
            {"hotCues", t.hotCues},
            {"memCues", t.memCues},
            {"beatGridLocked", t.beatGridLocked},
            {"color", t.color},
            {"rating", t.rating},
            {"comment", t.comment},
            {"comment2", t.comment2},
            {"tonality", t.tonality},
            {"mixName", t.mixName},
            {"remixer", t.remixer},
            {"dateAdded", t.dateAdded}
        };
    }

    friend void from_json(const nlohmann::json& j, PioneerTrack& t) {
        j.at("localTrackId").get_to(t.localTrackId);
        if (j.contains("source")) j.at("source").get_to(t.source);
        if (j.contains("externalId")) j.at("externalId").get_to(t.externalId);
        if (j.contains("externalPath")) j.at("externalPath").get_to(t.externalPath);
        if (j.contains("syncedAt")) j.at("syncedAt").get_to(t.syncedAt);
        if (j.contains("rekordboxId")) j.at("rekordboxId").get_to(t.rekordboxId);
        if (j.contains("playlistNames")) j.at("playlistNames").get_to(t.playlistNames);
        if (j.contains("hotCues")) j.at("hotCues").get_to(t.hotCues);
        if (j.contains("memCues")) j.at("memCues").get_to(t.memCues);
        if (j.contains("beatGridLocked")) j.at("beatGridLocked").get_to(t.beatGridLocked);
        if (j.contains("color")) j.at("color").get_to(t.color);
        if (j.contains("rating")) j.at("rating").get_to(t.rating);
        if (j.contains("comment")) j.at("comment").get_to(t.comment);
        if (j.contains("comment2")) j.at("comment2").get_to(t.comment2);
        if (j.contains("tonality")) j.at("tonality").get_to(t.tonality);
        if (j.contains("mixName")) j.at("mixName").get_to(t.mixName);
        if (j.contains("remixer")) j.at("remixer").get_to(t.remixer);
        if (j.contains("dateAdded")) j.at("dateAdded").get_to(t.dateAdded);
    }
};

} // namespace BeatMate::Models
