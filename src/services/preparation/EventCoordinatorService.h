#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include "../../models/Track.h"
#include "../../models/EventPlan.h"

namespace BeatMate::Services::Preparation {

struct CoordinatedSection {
    std::string name;
    std::string startTime;      // HH:MM
    std::string endTime;        // HH:MM
    double durationMinutes = 0.0;
    float targetEnergy = 5.0f;
    std::string genre;
    std::string djName;
    std::vector<Models::Track> assignedTracks;
    int trackCount = 0;
    std::string notes;
};

struct EventCoordination {
    int64_t eventId = 0;
    std::string eventName;
    std::vector<CoordinatedSection> sections;
    double totalDuration = 0.0;
    int totalTracks = 0;
    float coherenceScore = 0.0f;
    std::vector<std::string> warnings;
};

struct CoordinationConfig {
    bool autoAssignTracks = true;
    bool checkTransitions = true;
    float minTransitionQuality = 0.5f;
    int tracksPerHour = 15;
};

class EventCoordinatorService {
public:
    EventCoordinatorService() = default;

    EventCoordination coordinate(const Models::EventPlan& event, const std::vector<Models::Track>& trackPool,
                                  const CoordinationConfig& config = {});
    std::vector<CoordinatedSection> planSections(const Models::EventPlan& event);
    void assignTracksToSections(EventCoordination& coordination, const std::vector<Models::Track>& pool, const CoordinationConfig& config);
    float evaluateCoherence(const EventCoordination& coordination) const;
    std::vector<std::string> checkGaps(const EventCoordination& coordination) const;

private:
    double parseTime(const std::string& timeStr) const;
    double timeDiffMinutes(const std::string& startTime, const std::string& endTime) const;
    std::vector<Models::Track> selectTracksForSection(const std::vector<Models::Track>& pool,
                                                       const CoordinatedSection& section,
                                                       std::vector<bool>& used, int count);
};

} // namespace BeatMate::Services::Preparation
