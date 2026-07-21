#pragma once
#include <string>
#include <vector>
#include "../../models/EventPlan.h"
#include "../../models/Track.h"

namespace BeatMate::Services::Preparation {

struct EventValidationIssue {
    std::string section;
    std::string field;
    std::string message;
    std::string severity; // "error", "warning", "info"
};

struct EventValidationReport {
    bool valid = true;
    int errors = 0;
    int warnings = 0;
    float coveragePercent = 0.0f;      // how much of the event duration is covered by tracks
    float genreConsistency = 0.0f;     // 0-1
    float bpmConsistency = 0.0f;       // 0-1
    std::vector<EventValidationIssue> issues;
    std::string summary;
};

class EventValidationService {
public:
    EventValidationService() = default;

    EventValidationReport validate(const Models::EventPlan& event, const std::vector<Models::Track>& tracks);
    EventValidationReport validateDurations(const Models::EventPlan& event);
    EventValidationReport validateGenres(const Models::EventPlan& event, const std::vector<Models::Track>& tracks);
    EventValidationReport validateBpmRanges(const Models::EventPlan& event, const std::vector<Models::Track>& tracks);
    EventValidationReport validateEnergyFlow(const Models::EventPlan& event, const std::vector<Models::Track>& tracks);

private:
    void addIssue(EventValidationReport& report, const std::string& section,
                  const std::string& field, const std::string& message, const std::string& severity);
    double parseTimeToMinutes(const std::string& timeStr) const;
    double eventDurationMinutes(const Models::EventPlan& event) const;
};

} // namespace BeatMate::Services::Preparation
