#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <functional>
#include <tuple>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

enum class TrackSource : int {
    Local = 0,
    VirtualDJ = 1,
    Rekordbox = 2,
    Serato = 3,
    Traktor = 4,
    EngineDJ = 5,
    Streaming = 6
};

NLOHMANN_JSON_SERIALIZE_ENUM(TrackSource, {
    { TrackSource::Local, "Local" },
    { TrackSource::VirtualDJ, "VirtualDJ" },
    { TrackSource::Rekordbox, "Rekordbox" },
    { TrackSource::Serato, "Serato" },
    { TrackSource::Traktor, "Traktor" },
    { TrackSource::EngineDJ, "EngineDJ" },
    { TrackSource::Streaming, "Streaming" }
})

struct Track {
    int64_t id = 0;
    std::string filePath;
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    int year = 0;
    std::string comment;

    double duration = 0.0;          // seconds
    int sampleRate = 44100;
    int channels = 2;
    int bitRate = 320;              // kbps
    int bitDepth = 16;

    double bpm = 0.0;
    std::string key;               // toute notation (Camelot, Open Key, classique)
    float energy = 0.0f;           // 0-10 (use setEnergy() to clamp)

    int rating = 0;                 // 0-5
    int playCount = 0;
    int64_t lastPlayed = 0;         // unix timestamp

    std::string color;              // hex string e.g. "#FF0000"
    std::string label;
    std::string grouping;

    std::vector<uint8_t> albumArt; // non sérialisé dans NLOHMANN_DEFINE_TYPE

    int64_t dateAdded = 0;
    int64_t lastModified = 0;

    int64_t fileSize = 0;
    std::string fileFormat;         // e.g. "mp3", "flac", "wav"

    bool analyzed = false;
    int64_t analyzedDate = 0;

    std::string camelotKey;         // e.g. "8A"
    std::string openKey;            // e.g. "Am"

    std::string mood;
    float danceability = 0.0f;     // 0-1
    float lufs = 0.0f;             // integrated LUFS; 0.0 = not measured
    std::string energySegments;    // JSON [{"startTime":s,"endTime":s,"energy":1-10},...]
    float bpmConfidence = 0.0f;
    float keyConfidence = 0.0f;
    float truePeak = -100.0f;
    float loudnessRange = 0.0f;
    std::string sections;

    TrackSource source = TrackSource::Local;

    // Section markers (seconds from start; -1.0 = unset)
    double introStart = -1.0;
    double introEnd   = -1.0;
    double outroStart = -1.0;
    double outroEnd   = -1.0;

    std::string role;   // "Opener" | "Peak" | "Closer" | "Filler" | ""
    std::string venue;  // e.g. "Club", "Wedding", "Festival", "Bar"

    std::vector<std::string> myTags; // persistés via tables tags / track_tags

    Track() = default;

    Track(int64_t id, const std::string& filePath, const std::string& title, const std::string& artist)
        : id(id), filePath(filePath), title(title), artist(artist) {}

    void setEnergy(float v) noexcept {
        if (v < 0.0f) v = 0.0f;
        if (v > 10.0f) v = 10.0f;
        energy = v;
    }
    void setDanceability(float v) noexcept {
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        danceability = v;
    }

    bool operator==(const Track& other) const {
        if (id != 0 && other.id != 0) return id == other.id;
        return filePath == other.filePath && title == other.title;
    }
    bool operator!=(const Track& other) const { return !(*this == other); }
    bool operator<(const Track& other) const { return id < other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Track,
        id, filePath, title, artist, album, genre, year, comment,
        duration, sampleRate, channels, bitRate, bitDepth,
        bpm, key, energy,
        rating, playCount, lastPlayed,
        color, label, grouping,
        dateAdded, lastModified,
        fileSize, fileFormat,
        analyzed, analyzedDate,
        camelotKey, openKey,
        mood, danceability, lufs, energySegments,
        bpmConfidence, keyConfidence, truePeak, loudnessRange, sections,
        source,
        introStart, introEnd, outroStart, outroEnd,
        role, venue, myTags
    )
};

} // namespace BeatMate::Models

namespace std {
template<> struct hash<BeatMate::Models::Track> {
    size_t operator()(const BeatMate::Models::Track& t) const noexcept {
        return std::hash<int64_t>()(t.id);
    }
};
} // namespace std
