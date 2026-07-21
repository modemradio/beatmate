#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

enum class TransitionType : int {
    Cut = 0,
    Fade = 1,
    EQBlend = 2,
    FilterSweep = 3,
    Echo = 4,
    Backspin = 5,
    Slam = 6,
    HarmonicMix = 7,
    DoubleTime = 8,
    Custom = 9
};

NLOHMANN_JSON_SERIALIZE_ENUM(TransitionType, {
    { TransitionType::Cut, "Cut" },
    { TransitionType::Fade, "Fade" },
    { TransitionType::EQBlend, "EQBlend" },
    { TransitionType::FilterSweep, "FilterSweep" },
    { TransitionType::Echo, "Echo" },
    { TransitionType::Backspin, "Backspin" },
    { TransitionType::Slam, "Slam" },
    { TransitionType::HarmonicMix, "HarmonicMix" },
    { TransitionType::DoubleTime, "DoubleTime" },
    { TransitionType::Custom, "Custom" }
})

struct SetEntry {
    int64_t trackId = 0;
    double startTime = 0.0;         // absolute start time in the set (seconds)
    double duration = 0.0;          // planned play duration (seconds)
    TransitionType transitionType = TransitionType::Fade;
    double transitionDuration = 16.0; // transition duration (seconds/beats)
    std::string notes;

    // Track info cache (for display without DB lookup)
    std::string trackTitle;
    std::string trackArtist;
    double trackBpm = 0.0;
    std::string trackKey;
    float trackEnergy = 0.0f;

    double startCue = 0.0;         // where to start in the track
    double endCue = 0.0;           // where to end/transition out

    SetEntry() = default;

    SetEntry(int64_t trackId, double startTime, double duration)
        : trackId(trackId), startTime(startTime), duration(duration) {}

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SetEntry,
        trackId, startTime, duration, transitionType, transitionDuration, notes,
        trackTitle, trackArtist, trackBpm, trackKey, trackEnergy,
        startCue, endCue
    )
};

struct EnergyCurvePoint {
    double time = 0.0;             // time in the set (seconds)
    float energy = 0.0f;           // 0-10 energy level

    EnergyCurvePoint() = default;
    EnergyCurvePoint(double time, float energy) : time(time), energy(energy) {}

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(EnergyCurvePoint, time, energy)
};

struct SetPlan {
    int64_t id = 0;
    std::string name;
    int64_t eventId = 0;            // associated event (optional)

    std::vector<SetEntry> entries;

    double totalDuration = 0.0;     // total duration in seconds
    double avgBpm = 0.0;
    std::vector<EnergyCurvePoint> energyCurve;

    std::string genre;
    std::string notes;
    std::string color;
    int64_t createdAt = 0;
    int64_t modifiedAt = 0;

    double targetBpmMin = 0.0;
    double targetBpmMax = 0.0;
    float targetEnergyStart = 3.0f;
    float targetEnergyPeak = 8.0f;
    float targetEnergyEnd = 4.0f;

    SetPlan() = default;

    SetPlan(int64_t id, const std::string& name)
        : id(id), name(name) {}

    bool operator==(const SetPlan& other) const { return id == other.id; }

    [[nodiscard]] size_t trackCount() const { return entries.size(); }

    void recalculate() {
        totalDuration = 0.0;
        double bpmSum = 0.0;
        int bpmCount = 0;

        for (const auto& entry : entries) {
            totalDuration += entry.duration;
            if (entry.trackBpm > 0) {
                bpmSum += entry.trackBpm;
                ++bpmCount;
            }
        }

        avgBpm = bpmCount > 0 ? bpmSum / bpmCount : 0.0;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SetPlan,
        id, name, eventId, entries,
        totalDuration, avgBpm, energyCurve,
        genre, notes, color, createdAt, modifiedAt,
        targetBpmMin, targetBpmMax,
        targetEnergyStart, targetEnergyPeak, targetEnergyEnd
    )
};

} // namespace BeatMate::Models
