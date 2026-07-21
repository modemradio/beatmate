#pragma once

#include <string>
#include <vector>
#include <utility>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

enum class SoireeVenue : int {
    Club     = 0,
    Wedding  = 1,
    Festival = 2,
    Bar      = 3,
    Custom   = 4
};

NLOHMANN_JSON_SERIALIZE_ENUM(SoireeVenue, {
    { SoireeVenue::Club,     "Club"     },
    { SoireeVenue::Wedding,  "Wedding"  },
    { SoireeVenue::Festival, "Festival" },
    { SoireeVenue::Bar,      "Bar"      },
    { SoireeVenue::Custom,   "Custom"   }
})

struct PhaseTemplate {
    std::string name;                                     // e.g. "Club classic"
    SoireeVenue venue = SoireeVenue::Club;

    std::vector<float> phaseFractions;

    std::vector<std::pair<double, double>> bpmRanges;

    std::vector<std::pair<float, float>> energyRanges;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PhaseTemplate,
        name, venue, phaseFractions, bpmRanges, energyRanges)
};

// Client-provided must-play / do-not-play track-id lists.
struct ClientRequests {
    std::vector<int64_t> mustPlayIds;   // tracks the client explicitly requested
    std::vector<int64_t> doNotPlayIds;  // banned list

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ClientRequests,
        mustPlayIds, doNotPlayIds)
};

} // namespace BeatMate::Models
