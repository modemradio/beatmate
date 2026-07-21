#include "EventValidationService.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <numeric>

namespace BeatMate::Services::Preparation {

EventValidationReport EventValidationService::validate(
    const Models::EventPlan& event, const std::vector<Models::Track>& tracks) {

    EventValidationReport report;
    report.valid = true;

    if (event.name.empty()) {
        addIssue(report, "event", "name", "Event name is empty", "error");
    }
    if (event.startTime.empty() || event.endTime.empty()) {
        addIssue(report, "event", "time", "Start or end time not set", "warning");
    }
    if (event.sections.empty()) {
        addIssue(report, "event", "sections", "No sections defined", "warning");
    }

    auto durReport = validateDurations(event);
    report.issues.insert(report.issues.end(), durReport.issues.begin(), durReport.issues.end());

    auto genreReport = validateGenres(event, tracks);
    report.issues.insert(report.issues.end(), genreReport.issues.begin(), genreReport.issues.end());
    report.genreConsistency = genreReport.genreConsistency;

    auto bpmReport = validateBpmRanges(event, tracks);
    report.issues.insert(report.issues.end(), bpmReport.issues.begin(), bpmReport.issues.end());
    report.bpmConsistency = bpmReport.bpmConsistency;

    auto energyReport = validateEnergyFlow(event, tracks);
    report.issues.insert(report.issues.end(), energyReport.issues.begin(), energyReport.issues.end());

    double eventDuration = eventDurationMinutes(event);
    double tracksDuration = 0.0;
    for (const auto& t : tracks) tracksDuration += t.duration / 60.0;
    report.coveragePercent = eventDuration > 0 ? static_cast<float>(tracksDuration / eventDuration * 100.0) : 0.0f;

    if (report.coveragePercent < 80.0f) {
        addIssue(report, "event", "coverage",
                 "Track pool covers only " + std::to_string(static_cast<int>(report.coveragePercent)) + "% of event duration",
                 "warning");
    }

    report.errors = 0;
    report.warnings = 0;
    for (const auto& issue : report.issues) {
        if (issue.severity == "error") { ++report.errors; report.valid = false; }
        else if (issue.severity == "warning") ++report.warnings;
    }

    report.summary = "Event '" + event.name + "': " + std::to_string(report.errors) + " errors, " +
                     std::to_string(report.warnings) + " warnings, coverage=" +
                     std::to_string(static_cast<int>(report.coveragePercent)) + "%";

    spdlog::info("EventValidationService: {}", report.summary);
    return report;
}

EventValidationReport EventValidationService::validateDurations(const Models::EventPlan& event) {
    EventValidationReport report;
    double totalSectionDuration = 0.0;

    for (const auto& section : event.sections) {
        if (section.duration <= 0) {
            addIssue(report, section.name, "duration", "Section has no duration", "warning");
        } else if (section.duration < 10) {
            addIssue(report, section.name, "duration", "Section is very short (" +
                     std::to_string(static_cast<int>(section.duration)) + " min)", "info");
        }
        totalSectionDuration += section.duration;
    }

    double eventDuration = eventDurationMinutes(event);
    if (eventDuration > 0 && std::abs(totalSectionDuration - eventDuration) > 15.0) {
        addIssue(report, "event", "duration",
                 "Section durations (" + std::to_string(static_cast<int>(totalSectionDuration)) +
                 " min) don't match event duration (" + std::to_string(static_cast<int>(eventDuration)) + " min)",
                 "warning");
    }
    return report;
}

EventValidationReport EventValidationService::validateGenres(
    const Models::EventPlan& event, const std::vector<Models::Track>& tracks) {

    EventValidationReport report;
    if (tracks.empty()) { report.genreConsistency = 0.0f; return report; }

    std::set<std::string> expectedGenres;
    for (const auto& section : event.sections) {
        if (!section.genre.empty()) expectedGenres.insert(section.genre);
    }

    if (expectedGenres.empty()) {
        report.genreConsistency = 1.0f;
        return report;
    }

    int matching = 0;
    for (const auto& t : tracks) {
        if (expectedGenres.count(t.genre)) ++matching;
    }

    report.genreConsistency = static_cast<float>(matching) / static_cast<float>(tracks.size());

    if (report.genreConsistency < 0.5f) {
        addIssue(report, "event", "genre",
                 "Only " + std::to_string(static_cast<int>(report.genreConsistency * 100)) +
                 "% of tracks match expected genres", "warning");
    }
    return report;
}

EventValidationReport EventValidationService::validateBpmRanges(
    const Models::EventPlan& event, const std::vector<Models::Track>& tracks) {

    EventValidationReport report;
    if (tracks.empty()) { report.bpmConsistency = 0.0f; return report; }

    int inRange = 0;
    int total = 0;

    for (const auto& section : event.sections) {
        if (section.bpmMin <= 0 && section.bpmMax <= 0) continue;
        for (const auto& t : tracks) {
            if (t.bpm <= 0) continue;
            ++total;
            bool fits = true;
            if (section.bpmMin > 0 && t.bpm < section.bpmMin) fits = false;
            if (section.bpmMax > 0 && t.bpm > section.bpmMax) fits = false;
            if (fits) ++inRange;
        }
    }

    report.bpmConsistency = total > 0 ? static_cast<float>(inRange) / static_cast<float>(total) : 1.0f;

    if (report.bpmConsistency < 0.5f && total > 0) {
        addIssue(report, "event", "bpm",
                 "Only " + std::to_string(static_cast<int>(report.bpmConsistency * 100)) +
                 "% of tracks fit the expected BPM ranges", "warning");
    }
    return report;
}

EventValidationReport EventValidationService::validateEnergyFlow(
    const Models::EventPlan& event, const std::vector<Models::Track>& tracks) {

    EventValidationReport report;

    for (size_t i = 1; i < event.sections.size(); ++i) {
        float energyDiff = std::abs(event.sections[i].energy - event.sections[i - 1].energy);
        if (energyDiff > 4.0f) {
            addIssue(report, event.sections[i].name, "energy",
                     "Large energy jump from '" + event.sections[i - 1].name +
                     "' (E:" + std::to_string(static_cast<int>(event.sections[i - 1].energy)) +
                     ") to '" + event.sections[i].name +
                     "' (E:" + std::to_string(static_cast<int>(event.sections[i].energy)) + ")",
                     "warning");
        }
    }
    return report;
}

void EventValidationService::addIssue(EventValidationReport& report, const std::string& section,
                                       const std::string& field, const std::string& message,
                                       const std::string& severity) {
    EventValidationIssue issue;
    issue.section = section;
    issue.field = field;
    issue.message = message;
    issue.severity = severity;
    report.issues.push_back(issue);
}

double EventValidationService::parseTimeToMinutes(const std::string& timeStr) const {
    if (timeStr.size() < 3) return 0.0;
    try {
        size_t sep = timeStr.find(':');
        if (sep == std::string::npos) return 0.0;
        int hours = std::stoi(timeStr.substr(0, sep));
        int minutes = std::stoi(timeStr.substr(sep + 1));
        return hours * 60.0 + minutes;
    } catch (...) { return 0.0; }
}

double EventValidationService::eventDurationMinutes(const Models::EventPlan& event) const {
    double start = parseTimeToMinutes(event.startTime);
    double end = parseTimeToMinutes(event.endTime);
    if (end <= start) end += 24.0 * 60.0;
    return end - start;
}

}
