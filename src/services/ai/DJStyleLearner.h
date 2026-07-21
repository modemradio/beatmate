#pragma once
#include <string>
#include <vector>
#include <map>
#include "../../models/Track.h"

namespace BeatMate::Services::AI {

struct DJStylePreferences {
    double preferredBpmMin = 120.0, preferredBpmMax = 130.0;
    std::vector<std::string> preferredGenres;
    std::vector<std::string> preferredKeys;
    float preferredEnergyMin = 5.0f, preferredEnergyMax = 9.0f;
    std::map<std::string, float> genreWeights;
    float transitionStyleSmooth = 0.7f; // 0=hard cuts, 1=smooth blends
};

class DJStyleLearner {
public:
    DJStyleLearner() = default;
    ~DJStyleLearner() = default;
    DJStylePreferences learn(const std::vector<Models::Track>& playHistory);
    DJStylePreferences getPreferences() const { return preferences_; }
private:
    DJStylePreferences preferences_;
};

} // namespace BeatMate::Services::AI
