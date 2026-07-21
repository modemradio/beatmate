#pragma once
#include <string>
#include <vector>
#include "../../models/Track.h"

namespace BeatMate::Services::Preparation {

struct MatchDetail {
    std::string criterion;
    float score = 0.0f;
    std::string explanation;
    std::string recommendation;
};

struct MatchUpResult {
    Models::Track trackA;
    Models::Track trackB;
    float overallScore = 0.0f;
    std::string verdict;              // "Perfect", "Good", "Acceptable", "Poor", "Incompatible"
    std::string suggestedTransition;  // "Harmonic Mix", "EQ Blend", "Cut", etc.
    std::vector<MatchDetail> details;
    double suggestedMixPoint = 0.0;   // seconds from end of track A
    float mixDurationBeats = 16.0f;
};

class MatchUpService {
public:
    MatchUpService() = default;

    MatchUpResult matchTracks(const Models::Track& a, const Models::Track& b);
    float quickScore(const Models::Track& a, const Models::Track& b);
    std::string suggestTransitionType(const MatchUpResult& match);
    std::vector<MatchUpResult> findBestMatches(const Models::Track& reference, const std::vector<Models::Track>& pool, int topN = 10);

private:
    float computeBpmMatch(const Models::Track& a, const Models::Track& b, MatchDetail& detail) const;
    float computeKeyMatch(const Models::Track& a, const Models::Track& b, MatchDetail& detail) const;
    float computeEnergyMatch(const Models::Track& a, const Models::Track& b, MatchDetail& detail) const;
    float computeGenreMatch(const Models::Track& a, const Models::Track& b, MatchDetail& detail) const;
    float computeMoodMatch(const Models::Track& a, const Models::Track& b, MatchDetail& detail) const;
    float computeTimbreMatch(const Models::Track& a, const Models::Track& b, MatchDetail& detail) const;
    std::string verdictFromScore(float score) const;
    bool isKeyCompatible(const std::string& key1, const std::string& key2) const;
    std::string keyRelation(const std::string& key1, const std::string& key2) const;
};

} // namespace BeatMate::Services::Preparation
