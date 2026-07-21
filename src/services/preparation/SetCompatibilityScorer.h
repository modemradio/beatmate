#pragma once

#include <string>
#include <vector>
#include "../../models/Track.h"

namespace BeatMate::Services::Preparation {

class SetCompatibilityScorer {
public:
    struct CompatibilityResult {
        int score = 0;          // 0-100
        int keyScore = 0;       // 0-30
        int bpmScore = 0;       // 0-25
        int energyScore = 0;    // 0-20
        int genreScore = 0;     // 0-15
        int varietyScore = 0;   // 0-10
        std::string keyInfo;    // "Compatible (same key)" etc.
        std::string bpmInfo;
        std::string advice;     // transition advice
    };

    CompatibilityResult score(const Models::Track& current, const Models::Track& next);

    struct Suggestion {
        int64_t trackId;
        int score;
        std::string reason;
    };
    std::vector<Suggestion> suggestNext(const Models::Track& current,
                                         const std::vector<Models::Track>& pool,
                                         int maxResults = 10);

    std::vector<int64_t> autoOrder(const std::vector<Models::Track>& tracks);

private:
    int scoreKey(const std::string& key1, const std::string& key2);
    int scoreBpm(float bpm1, float bpm2);
    int scoreEnergy(float e1, float e2);
    int scoreGenre(const std::string& g1, const std::string& g2);
    int camelotNumber(const std::string& key);
    char camelotLetter(const std::string& key);
};

} // namespace BeatMate::Services::Preparation
