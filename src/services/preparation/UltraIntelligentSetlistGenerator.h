#pragma once
#include <string>
#include <vector>
#include <functional>
#include "../../models/Track.h"

namespace BeatMate::Services::Preparation {

struct SetlistConstraints {
    double durationMinutes = 60.0;
    float energyArcStart = 3.0f;
    float energyArcPeak = 8.5f;
    float energyArcEnd = 4.0f;
    double bpmRangeMin = 0.0;
    double bpmRangeMax = 999.0;
    std::vector<std::string> preferredGenres;
    bool enforceHarmonicMixing = true;
    bool enforceEnergyArc = true;
    float diversityFactor = 0.3f;
    float coherenceFactor = 0.7f;
};

struct SetlistTrackScore {
    Models::Track track;
    float harmonicScore = 0.0f;
    float energyFitScore = 0.0f;
    float bpmFlowScore = 0.0f;
    float genreScore = 0.0f;
    float diversityBonus = 0.0f;
    float totalScore = 0.0f;
};

struct GeneratedSetlist {
    std::vector<SetlistTrackScore> scoredTracks;
    float overallHarmony = 0.0f;
    float overallEnergyFit = 0.0f;
    float overallCoherence = 0.0f;
    float totalScore = 0.0f;
    double totalDuration = 0.0;
    int trackCount = 0;
    std::string summary;
};

using ProgressCallback = std::function<void(float progress, const std::string& status)>;

class UltraIntelligentSetlistGenerator {
public:
    UltraIntelligentSetlistGenerator() = default;

    GeneratedSetlist generate(const std::vector<Models::Track>& pool, const SetlistConstraints& constraints);
    GeneratedSetlist generateWithProgress(const std::vector<Models::Track>& pool, const SetlistConstraints& constraints, ProgressCallback callback);

private:
    float targetEnergyAtPosition(float position, const SetlistConstraints& constraints) const;
    float harmonicScoreBetween(const Models::Track& a, const Models::Track& b) const;
    float bpmFlowScore(double prevBpm, double nextBpm) const;
    float genreMatchScore(const Models::Track& track, const std::vector<std::string>& preferredGenres) const;
    float diversityScore(const Models::Track& track, const std::vector<Models::Track>& selected) const;
    SetlistTrackScore scoreTrack(const Models::Track& track, const Models::Track& prev,
                                  float targetEnergy, float position, const SetlistConstraints& constraints,
                                  const std::vector<Models::Track>& selected);
    bool isKeyCompatible(const std::string& key1, const std::string& key2) const;
    GeneratedSetlist buildResult(const std::vector<SetlistTrackScore>& scored, const SetlistConstraints& constraints);
};

}
