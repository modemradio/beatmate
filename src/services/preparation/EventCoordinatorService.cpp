#include "EventCoordinatorService.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace BeatMate::Services::Preparation {

EventCoordination EventCoordinatorService::coordinate(
    const Models::EventPlan& event, const std::vector<Models::Track>& trackPool,
    const CoordinationConfig& config) {

    EventCoordination coordination;
    coordination.eventId = event.id;
    coordination.eventName = event.name;
    coordination.sections = planSections(event);

    if (config.autoAssignTracks) {
        assignTracksToSections(coordination, trackPool, config);
    }

    coordination.totalDuration = 0.0;
    coordination.totalTracks = 0;
    for (const auto& s : coordination.sections) {
        coordination.totalDuration += s.durationMinutes;
        coordination.totalTracks += s.trackCount;
    }

    coordination.coherenceScore = evaluateCoherence(coordination);
    coordination.warnings = checkGaps(coordination);

    spdlog::info("EventCoordinatorService: Coordinated '{}' - {} sections, {} tracks, coherence={:.0f}%",
                 event.name, coordination.sections.size(), coordination.totalTracks, coordination.coherenceScore * 100.0f);
    return coordination;
}

std::vector<CoordinatedSection> EventCoordinatorService::planSections(const Models::EventPlan& event) {
    std::vector<CoordinatedSection> sections;

    if (event.sections.empty()) {
        double totalMinutes = timeDiffMinutes(event.startTime, event.endTime);
        if (totalMinutes <= 0) totalMinutes = 180.0;

        CoordinatedSection warmup;
        warmup.name = "Warm-up";
        warmup.startTime = event.startTime;
        warmup.durationMinutes = totalMinutes * 0.25;
        warmup.targetEnergy = 4.0f;
        warmup.genre = "Deep House";
        sections.push_back(warmup);

        CoordinatedSection peakTime;
        peakTime.name = "Peak Time";
        peakTime.durationMinutes = totalMinutes * 0.5;
        peakTime.targetEnergy = 8.0f;
        peakTime.genre = "House";
        sections.push_back(peakTime);

        CoordinatedSection cooldown;
        cooldown.name = "Cool Down";
        cooldown.durationMinutes = totalMinutes * 0.25;
        cooldown.targetEnergy = 4.0f;
        cooldown.genre = "Chill";
        sections.push_back(cooldown);
    } else {
        for (const auto& es : event.sections) {
            CoordinatedSection cs;
            cs.name = es.name;
            cs.startTime = es.startTime;
            cs.durationMinutes = es.duration;
            cs.targetEnergy = es.energy;
            cs.genre = es.genre;
            cs.notes = es.notes;
            sections.push_back(cs);
        }
    }
    return sections;
}

void EventCoordinatorService::assignTracksToSections(
    EventCoordination& coordination, const std::vector<Models::Track>& pool, const CoordinationConfig& config) {

    std::vector<bool> used(pool.size(), false);

    for (auto& section : coordination.sections) {
        int trackCount = static_cast<int>(section.durationMinutes / 60.0 * config.tracksPerHour);
        trackCount = std::max(1, trackCount);
        section.assignedTracks = selectTracksForSection(pool, section, used, trackCount);
        section.trackCount = static_cast<int>(section.assignedTracks.size());
    }
}

float EventCoordinatorService::evaluateCoherence(const EventCoordination& coordination) const {
    if (coordination.sections.empty()) return 0.0f;

    float totalCoherence = 0.0f;
    int checks = 0;

    for (const auto& section : coordination.sections) {
        if (section.assignedTracks.empty()) continue;

        float avgEnergy = 0.0f;
        for (const auto& t : section.assignedTracks) avgEnergy += t.energy;
        avgEnergy /= static_cast<float>(section.assignedTracks.size());

        float energyFit = 1.0f - std::abs(avgEnergy - section.targetEnergy) / 10.0f;
        totalCoherence += std::max(0.0f, energyFit);
        ++checks;

        if (section.assignedTracks.size() > 1) {
            float bpmCoherence = 0.0f;
            for (size_t i = 1; i < section.assignedTracks.size(); ++i) {
                double diff = std::abs(section.assignedTracks[i].bpm - section.assignedTracks[i - 1].bpm);
                bpmCoherence += (diff <= 6.0) ? 1.0f : 0.0f;
            }
            bpmCoherence /= static_cast<float>(section.assignedTracks.size() - 1);
            totalCoherence += bpmCoherence;
            ++checks;
        }
    }

    return checks > 0 ? totalCoherence / static_cast<float>(checks) : 0.0f;
}

std::vector<std::string> EventCoordinatorService::checkGaps(const EventCoordination& coordination) const {
    std::vector<std::string> warnings;

    for (size_t i = 0; i < coordination.sections.size(); ++i) {
        const auto& s = coordination.sections[i];
        if (s.assignedTracks.empty()) {
            warnings.push_back("Section '" + s.name + "' has no tracks assigned");
        }
        if (s.durationMinutes <= 0) {
            warnings.push_back("Section '" + s.name + "' has no duration set");
        }

        if (i > 0 && !coordination.sections[i - 1].assignedTracks.empty() && !s.assignedTracks.empty()) {
            const auto& lastTrack = coordination.sections[i - 1].assignedTracks.back();
            const auto& firstTrack = s.assignedTracks.front();
            double bpmDiff = std::abs(lastTrack.bpm - firstTrack.bpm);
            float energyDiff = std::abs(lastTrack.energy - firstTrack.energy);

            if (bpmDiff > 15.0) {
                warnings.push_back("Large BPM gap between '" + coordination.sections[i - 1].name +
                                   "' and '" + s.name + "' (" + std::to_string(static_cast<int>(bpmDiff)) + " BPM)");
            }
            if (energyDiff > 4.0f) {
                warnings.push_back("Large energy gap between '" + coordination.sections[i - 1].name +
                                   "' and '" + s.name + "'");
            }
        }
    }
    return warnings;
}

std::vector<Models::Track> EventCoordinatorService::selectTracksForSection(
    const std::vector<Models::Track>& pool, const CoordinatedSection& section,
    std::vector<bool>& used, int count) {

    struct Candidate { size_t index; float score; };
    std::vector<Candidate> candidates;

    for (size_t i = 0; i < pool.size(); ++i) {
        if (used[i]) continue;
        float score = 0.0f;
        float energyDiff = std::abs(pool[i].energy - section.targetEnergy);
        score += (1.0f - energyDiff / 10.0f) * 0.5f;
        if (!section.genre.empty() && pool[i].genre == section.genre) score += 0.3f;
        if (pool[i].duration > 0) score += 0.2f;
        candidates.push_back({i, score});
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.score > b.score; });

    std::vector<Models::Track> selected;
    for (const auto& c : candidates) {
        if (static_cast<int>(selected.size()) >= count) break;
        selected.push_back(pool[c.index]);
        used[c.index] = true;
    }
    return selected;
}

double EventCoordinatorService::parseTime(const std::string& timeStr) const {
    if (timeStr.size() < 4) return 0.0;
    try {
        size_t sep = timeStr.find(':');
        if (sep == std::string::npos) return 0.0;
        int hours = std::stoi(timeStr.substr(0, sep));
        int minutes = std::stoi(timeStr.substr(sep + 1));
        return hours * 60.0 + minutes;
    } catch (...) { return 0.0; }
}

double EventCoordinatorService::timeDiffMinutes(const std::string& startTime, const std::string& endTime) const {
    double start = parseTime(startTime);
    double end = parseTime(endTime);
    if (end <= start) end += 24.0 * 60.0; // la soiree passe minuit
    return end - start;
}

} // namespace BeatMate::Services::Preparation
