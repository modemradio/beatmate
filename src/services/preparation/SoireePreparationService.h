#pragma once
#include <string>
#include <vector>
#include "../../models/Track.h"
#include "../../models/EventPlan.h"

namespace BeatMate::Services::Preparation {

enum class SoireeType { Club, Lounge, Festival, PrivateParty, Wedding, Corporate, AfterParty };

struct SoireeProfile {
    SoireeType type = SoireeType::Club;
    std::string name;
    int expectedGuests = 100;
    double durationHours = 4.0;
    std::string startTime;
    std::vector<std::string> genres;
    float peakEnergyTarget = 8.0f;
    bool hasWarmup = true;
    bool hasCooldown = true;
    std::string ambiance;   // "intimate", "energetic", "chill", "wild"
};

struct SoireePlan {
    SoireeProfile profile;
    std::vector<Models::Track> playlist;
    std::vector<std::string> phases;
    double totalDuration = 0.0;
    int trackCount = 0;
    float qualityScore = 0.0f;
    std::string description;
    std::map<std::string, int> genreDistribution;
};

class SoireePreparationService {
public:
    SoireePreparationService() = default;

    SoireePlan prepareSoiree(const std::vector<Models::Track>& pool, const SoireeProfile& profile);
    SoireePlan prepareClubNight(const std::vector<Models::Track>& pool, double hours, const std::string& genre);
    SoireePlan prepareLounge(const std::vector<Models::Track>& pool, double hours);
    SoireePlan prepareFestivalSet(const std::vector<Models::Track>& pool, double hours);
    SoireePlan prepareWedding(const std::vector<Models::Track>& pool, double hours);

    static SoireeProfile defaultProfile(SoireeType type);

private:
    struct PhaseConfig {
        std::string name;
        float energyStart;
        float energyEnd;
        float durationRatio;
    };

    std::vector<PhaseConfig> getPhasesForType(SoireeType type) const;
    std::vector<Models::Track> selectTracksForPhase(const std::vector<Models::Track>& pool,
                                                     const PhaseConfig& phase, double phaseDuration,
                                                     std::vector<bool>& used);
    float evaluatePlan(const SoireePlan& plan) const;
};

} // namespace BeatMate::Services::Preparation
