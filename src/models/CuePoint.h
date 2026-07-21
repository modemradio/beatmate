#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <tuple>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

enum class CuePointType : int {
    HotCue = 0,
    MemoryCue = 1,
    Loop = 2,
    Grid = 3,
    IntroStart = 4,
    IntroEnd   = 5,
    OutroStart = 6,
    OutroEnd   = 7
};

NLOHMANN_JSON_SERIALIZE_ENUM(CuePointType, {
    { CuePointType::HotCue, "HotCue" },
    { CuePointType::MemoryCue, "MemoryCue" },
    { CuePointType::Loop, "Loop" },
    { CuePointType::Grid, "Grid" },
    { CuePointType::IntroStart, "IntroStart" },
    { CuePointType::IntroEnd,   "IntroEnd" },
    { CuePointType::OutroStart, "OutroStart" },
    { CuePointType::OutroEnd,   "OutroEnd" }
})

struct CuePoint {
    int64_t id = 0;
    int64_t trackId = 0;

    CuePointType type = CuePointType::HotCue;

    double position = 0.0;     // seconds
    double length = 0.0;       // seconds (for loops)

    std::string name;
    std::string color;         // hex string e.g. "#FF0000"

    int number = 0;            // 1-8 for hot cues, 0 if unassigned

    CuePoint() = default;

    CuePoint(int64_t id, int64_t trackId, CuePointType type, double position)
        : id(id), trackId(trackId), type(type), position(position) {}

    CuePoint(int64_t id, int64_t trackId, CuePointType type, double position,
             double length, const std::string& name, const std::string& color, int number)
        : id(id), trackId(trackId), type(type), position(position),
          length(length), name(name), color(color), number(number) {}

    bool operator==(const CuePoint& other) const {
        return std::tie(id, position) == std::tie(other.id, other.position);
    }
    bool operator!=(const CuePoint& other) const { return !(*this == other); }

    bool operator<(const CuePoint& other) const {
        return std::tie(position, id) < std::tie(other.position, other.id);
    }

    [[nodiscard]] bool isLoop() const { return type == CuePointType::Loop || length > 0.0; }

    [[nodiscard]] double endPosition() const { return position + length; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CuePoint,
        id, trackId, type, position, length,
        name, color, number
    )
};

} // namespace BeatMate::Models
