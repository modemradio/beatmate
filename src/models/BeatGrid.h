#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

struct BeatMarker {
    double position = 0.0;      // time in seconds
    float strength = 1.0f;      // beat strength 0-1
    int barPosition = 0;        // position within bar (0-3 for 4/4 time)

    BeatMarker() = default;
    BeatMarker(double position, float strength, int barPosition)
        : position(position), strength(strength), barPosition(barPosition) {}

    bool operator==(const BeatMarker& other) const {
        return std::abs(position - other.position) < 1e-6;
    }

    bool operator<(const BeatMarker& other) const {
        return position < other.position;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BeatMarker, position, strength, barPosition)
};

struct TempoChange {
    double position = 0.0;      // time in seconds
    double bpm = 0.0;           // new tempo at this position

    TempoChange() = default;
    TempoChange(double position, double bpm) : position(position), bpm(bpm) {}

    bool operator<(const TempoChange& other) const {
        return position < other.position;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(TempoChange, position, bpm)
};

struct BeatGrid {
    int64_t trackId = 0;

    double bpm = 0.0;
    double firstBeatOffset = 0.0;   // time of first beat in seconds (>= 0)

    std::vector<BeatMarker> beats;

    bool isVariable = false;
    std::vector<TempoChange> tempoChanges;

    int beatsPerBar = 4;
    int beatUnit = 4;               // denominator (4 = quarter note)

    BeatGrid() = default;
    explicit BeatGrid(int64_t trackId) : trackId(trackId) {}

    BeatGrid(int64_t trackId, double bpm, double firstBeatOffset)
        : trackId(trackId), bpm(bpm) {
        setFirstBeatOffset(firstBeatOffset);
    }

    bool operator==(const BeatGrid& other) const { return trackId == other.trackId; }

    void setFirstBeatOffset(double v) noexcept {
        firstBeatOffset = (v < 0.0) ? 0.0 : v;
    }
    void setBeatsPerBar(int v) noexcept {
        beatsPerBar = (v > 0) ? v : 4;
    }
    void setBeatUnit(int v) noexcept {
        beatUnit = (v > 0) ? v : 4;
    }

    [[nodiscard]] double bpmAtPosition(double position) const {
        if (!isVariable || tempoChanges.empty()) return bpm;
        // Premier tempoChange > position ; le précédent s'applique
        auto it = std::lower_bound(
            tempoChanges.begin(), tempoChanges.end(), position,
            [](const TempoChange& tc, double p) { return tc.position <= p; });
        if (it == tempoChanges.begin()) return bpm; // before any change
        --it;
        return it->bpm;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BeatGrid,
        trackId, bpm, firstBeatOffset,
        beats, isVariable, tempoChanges,
        beatsPerBar, beatUnit
    )
};

} // namespace BeatMate::Models
