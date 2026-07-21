#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>
#include "DJSoftwareTrack.h"

namespace BeatMate::Models {

struct SeratoFlipAction {
    double time = 0.0;
    std::string type;           // "censor", "jump", etc.
    double parameter = 0.0;
    std::string name;

    SeratoFlipAction() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SeratoFlipAction,
        time, type, parameter, name
    )
};

struct SeratoFlip {
    std::string name;
    bool enabled = true;
    std::vector<SeratoFlipAction> actions;

    SeratoFlip() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SeratoFlip, name, enabled, actions)
};

struct SeratoTrack : public DJSoftwareTrack {
    std::string seratoId;
    std::vector<std::string> crateNames;
    std::vector<SeratoFlip> flipData;

    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    std::string label;
    std::string comment;
    std::string grouping;
    std::string key;
    std::string camelotKey;
    double bpm = 0.0;
    double duration = 0.0;
    int    year = 0;
    int    rating = 0;
    int    playCount = 0;
    int64_t lastPlayed = 0;
    std::string seratoDateAdded;

    std::string seratoAnalysis;
    std::vector<uint8_t> seratoBeatGrid;
    std::vector<uint8_t> seratoMarkers;
    std::vector<uint8_t> seratoMarkers2;

    std::vector<uint8_t> seratoOverview;

    int seratoPlayCount = 0;
    bool seratoAnalyzed = false;
    std::string seratoColor;
    int seratoBpmLock = 0;

    SeratoTrack() {
        source = TrackSource::Serato;
    }

    explicit SeratoTrack(const std::string& seratoId)
        : seratoId(seratoId) {
        source = TrackSource::Serato;
    }

    friend void to_json(nlohmann::json& j, const SeratoTrack& t) {
        j = nlohmann::json{
            {"localTrackId", t.localTrackId},
            {"source", t.source},
            {"externalId", t.externalId},
            {"externalPath", t.externalPath},
            {"syncedAt", t.syncedAt},
            {"seratoId", t.seratoId},
            {"crateNames", t.crateNames},
            {"flipData", t.flipData},
            {"seratoAnalysis", t.seratoAnalysis},
            {"seratoPlayCount", t.seratoPlayCount},
            {"seratoAnalyzed", t.seratoAnalyzed},
            {"seratoColor", t.seratoColor},
            {"seratoBpmLock", t.seratoBpmLock},
            {"title", t.title},
            {"artist", t.artist},
            {"album", t.album},
            {"genre", t.genre},
            {"label", t.label},
            {"comment", t.comment},
            {"grouping", t.grouping},
            {"key", t.key},
            {"camelotKey", t.camelotKey},
            {"bpm", t.bpm},
            {"duration", t.duration},
            {"year", t.year},
            {"rating", t.rating},
            {"playCount", t.playCount},
            {"lastPlayed", t.lastPlayed},
            {"seratoDateAdded", t.seratoDateAdded}
        };
    }

    friend void from_json(const nlohmann::json& j, SeratoTrack& t) {
        if (j.contains("localTrackId")) j.at("localTrackId").get_to(t.localTrackId);
        if (j.contains("source")) j.at("source").get_to(t.source);
        if (j.contains("externalId")) j.at("externalId").get_to(t.externalId);
        if (j.contains("externalPath")) j.at("externalPath").get_to(t.externalPath);
        if (j.contains("syncedAt")) j.at("syncedAt").get_to(t.syncedAt);
        if (j.contains("seratoId")) j.at("seratoId").get_to(t.seratoId);
        if (j.contains("crateNames")) j.at("crateNames").get_to(t.crateNames);
        if (j.contains("flipData")) j.at("flipData").get_to(t.flipData);
        if (j.contains("seratoAnalysis")) j.at("seratoAnalysis").get_to(t.seratoAnalysis);
        if (j.contains("seratoPlayCount")) j.at("seratoPlayCount").get_to(t.seratoPlayCount);
        if (j.contains("seratoAnalyzed")) j.at("seratoAnalyzed").get_to(t.seratoAnalyzed);
        if (j.contains("seratoColor")) j.at("seratoColor").get_to(t.seratoColor);
        if (j.contains("seratoBpmLock")) j.at("seratoBpmLock").get_to(t.seratoBpmLock);
        if (j.contains("title")) j.at("title").get_to(t.title);
        if (j.contains("artist")) j.at("artist").get_to(t.artist);
        if (j.contains("album")) j.at("album").get_to(t.album);
        if (j.contains("genre")) j.at("genre").get_to(t.genre);
        if (j.contains("label")) j.at("label").get_to(t.label);
        if (j.contains("comment")) j.at("comment").get_to(t.comment);
        if (j.contains("grouping")) j.at("grouping").get_to(t.grouping);
        if (j.contains("key")) j.at("key").get_to(t.key);
        if (j.contains("camelotKey")) j.at("camelotKey").get_to(t.camelotKey);
        if (j.contains("bpm")) j.at("bpm").get_to(t.bpm);
        if (j.contains("duration")) j.at("duration").get_to(t.duration);
        if (j.contains("year")) j.at("year").get_to(t.year);
        if (j.contains("rating")) j.at("rating").get_to(t.rating);
        if (j.contains("playCount")) j.at("playCount").get_to(t.playCount);
        if (j.contains("lastPlayed")) j.at("lastPlayed").get_to(t.lastPlayed);
        if (j.contains("seratoDateAdded")) j.at("seratoDateAdded").get_to(t.seratoDateAdded);
    }
};

} // namespace BeatMate::Models
