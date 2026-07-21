#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

struct HandoverPoint
{
    std::string id;              // UUID-ish
    double      atMinutes = 0.0; // absolute minute from event start
    std::string fromDjName;
    std::string toDjName;
    std::string notes;
    int         colorARGB = 0xFFFF8844;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(HandoverPoint,
        id, atMinutes, fromDjName, toDjName, notes, colorARGB)
};

} // namespace BeatMate::Models
