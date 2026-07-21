#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace BeatMate::Models {

struct DJProfile {
    std::string name;
    std::string venue;
    std::array<double, 10> weights {
        0.28, 0.20, 0.14, 0.10, 0.05, 0.05, 0.08, 0.05, 0.02, 0.03
    };
    double      minBPM = 90.0;
    double      maxBPM = 160.0;
    std::string genreFilter;
    std::string energyDirection = "Auto";
    std::vector<int64_t> favorites;
    std::vector<int64_t> skipped;
    std::vector<std::array<int64_t, 2>> associations;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(DJProfile,
        name, venue, weights, minBPM, maxBPM, genreFilter, energyDirection,
        favorites, skipped, associations)
};

}
