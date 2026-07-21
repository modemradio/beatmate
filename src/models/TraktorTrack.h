#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>
#include "DJSoftwareTrack.h"

namespace BeatMate::Models {

struct TraktorPlaylistEntry {
    std::string playlistPath;   // Traktor uses folder paths like "/Playlists/My Set/"
    int position = 0;

    TraktorPlaylistEntry() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(TraktorPlaylistEntry,
        playlistPath, position
    )
};

struct TraktorStripeData {
    std::vector<uint8_t> stripeData;    // Traktor stripe (mini-waveform) binary
    int stripeResolution = 0;
    int stripeLength = 0;

    TraktorStripeData() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(TraktorStripeData,
        stripeResolution, stripeLength
    )
};

struct TraktorTrack : public DJSoftwareTrack {
    std::string traktorId;                          // Traktor internal ID / audio_id
    TraktorStripeData stripeData;                    // Traktor mini-waveform
    std::vector<TraktorPlaylistEntry> playlistEntries;

    std::string musicalKey;                         // Traktor key notation
    float lockGain = 0.0f;                          // auto-gain in dB
    bool gridLocked = false;
    std::string importDate;
    std::string lastPlayDate;
    int traktorPlayCount = 0;
    int traktorRating = 0;                          // 0-255 in Traktor
    std::string traktorColor;                       // color index

    double traktorBpm = 0.0;
    double traktorBpmQuality = 0.0;
    double gridOffset = 0.0;                        // beat grid offset

    struct TraktorCue {
        int type = 0;                               // 0=cue, 1=fadein, 2=fadeout, 3=load, 4=grid, 5=loop
        double start = 0.0;
        double length = 0.0;
        int hotcue = -1;
        std::string name;
        int colorIndex = 0;

        TraktorCue() = default;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(TraktorCue,
            type, start, length, hotcue, name, colorIndex
        )
    };

    std::vector<TraktorCue> traktorCues;

    std::string volume;                             // volume name
    std::string directory;                          // directory path
    std::string filename;                           // filename only

    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    std::string label;
    std::string comment;
    int    year = 0;
    double durationSec = 0.0;

    TraktorTrack() {
        source = TrackSource::Traktor;
    }

    explicit TraktorTrack(const std::string& traktorId)
        : traktorId(traktorId) {
        source = TrackSource::Traktor;
    }

    friend void to_json(nlohmann::json& j, const TraktorTrack& t) {
        j = nlohmann::json{
            {"localTrackId", t.localTrackId},
            {"source", t.source},
            {"externalId", t.externalId},
            {"externalPath", t.externalPath},
            {"syncedAt", t.syncedAt},
            {"traktorId", t.traktorId},
            {"stripeData", t.stripeData},
            {"playlistEntries", t.playlistEntries},
            {"musicalKey", t.musicalKey},
            {"lockGain", t.lockGain},
            {"gridLocked", t.gridLocked},
            {"importDate", t.importDate},
            {"lastPlayDate", t.lastPlayDate},
            {"traktorPlayCount", t.traktorPlayCount},
            {"traktorRating", t.traktorRating},
            {"traktorColor", t.traktorColor},
            {"traktorBpm", t.traktorBpm},
            {"traktorBpmQuality", t.traktorBpmQuality},
            {"gridOffset", t.gridOffset},
            {"traktorCues", t.traktorCues},
            {"volume", t.volume},
            {"directory", t.directory},
            {"filename", t.filename}
        };
    }

    friend void from_json(const nlohmann::json& j, TraktorTrack& t) {
        if (j.contains("localTrackId")) j.at("localTrackId").get_to(t.localTrackId);
        if (j.contains("source")) j.at("source").get_to(t.source);
        if (j.contains("externalId")) j.at("externalId").get_to(t.externalId);
        if (j.contains("externalPath")) j.at("externalPath").get_to(t.externalPath);
        if (j.contains("syncedAt")) j.at("syncedAt").get_to(t.syncedAt);
        if (j.contains("traktorId")) j.at("traktorId").get_to(t.traktorId);
        if (j.contains("stripeData")) j.at("stripeData").get_to(t.stripeData);
        if (j.contains("playlistEntries")) j.at("playlistEntries").get_to(t.playlistEntries);
        if (j.contains("musicalKey")) j.at("musicalKey").get_to(t.musicalKey);
        if (j.contains("lockGain")) j.at("lockGain").get_to(t.lockGain);
        if (j.contains("gridLocked")) j.at("gridLocked").get_to(t.gridLocked);
        if (j.contains("importDate")) j.at("importDate").get_to(t.importDate);
        if (j.contains("lastPlayDate")) j.at("lastPlayDate").get_to(t.lastPlayDate);
        if (j.contains("traktorPlayCount")) j.at("traktorPlayCount").get_to(t.traktorPlayCount);
        if (j.contains("traktorRating")) j.at("traktorRating").get_to(t.traktorRating);
        if (j.contains("traktorColor")) j.at("traktorColor").get_to(t.traktorColor);
        if (j.contains("traktorBpm")) j.at("traktorBpm").get_to(t.traktorBpm);
        if (j.contains("traktorBpmQuality")) j.at("traktorBpmQuality").get_to(t.traktorBpmQuality);
        if (j.contains("gridOffset")) j.at("gridOffset").get_to(t.gridOffset);
        if (j.contains("traktorCues")) j.at("traktorCues").get_to(t.traktorCues);
        if (j.contains("volume")) j.at("volume").get_to(t.volume);
        if (j.contains("directory")) j.at("directory").get_to(t.directory);
        if (j.contains("filename")) j.at("filename").get_to(t.filename);
    }
};

} // namespace BeatMate::Models
