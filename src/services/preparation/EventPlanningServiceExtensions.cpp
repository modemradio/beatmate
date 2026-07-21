#include "EventPlanningServiceExtensions.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace BeatMate::Services::Preparation {

MultiRoomPlan EventPlanningServiceExtensions::planMultiRoom(
    const Models::EventPlan& event, const std::vector<RoomConfig>& roomConfigs) {

    MultiRoomPlan plan;
    plan.eventName = event.name;
    plan.rooms = roomConfigs;
    plan.totalDJs = 0;
    plan.totalTracks = 0;

    double eventStart = parseTime(event.startTime);
    double eventEnd = parseTime(event.endTime);
    if (eventEnd <= eventStart) eventEnd += 24.0 * 60.0;
    plan.totalDuration = eventEnd - eventStart;

    for (auto& room : plan.rooms) {
        plan.totalDJs += static_cast<int>(room.djSlots.size());
        for (const auto& slot : room.djSlots) {
            plan.totalTracks += static_cast<int>(slot.tracks.size());
        }
    }

    spdlog::info("EventPlanningServiceExtensions: Multi-room plan '{}' - {} rooms, {} DJs",
                 plan.eventName, plan.rooms.size(), plan.totalDJs);
    return plan;
}

void EventPlanningServiceExtensions::assignDJsToSlots(MultiRoomPlan& plan, const std::vector<DJSlot>& djPool) {
    size_t djIdx = 0;
    for (auto& room : plan.rooms) {
        if (room.djSlots.empty()) {
            double roomDuration = 0;
            for (const auto& slot : room.djSlots) roomDuration += slot.durationMinutes;
            if (roomDuration <= 0) roomDuration = plan.totalDuration;

            double slotDuration = 120.0; // 2 hours per DJ
            int numSlots = std::max(1, static_cast<int>(roomDuration / slotDuration));

            for (int i = 0; i < numSlots && djIdx < djPool.size(); ++i) {
                DJSlot slot = djPool[djIdx++];
                slot.durationMinutes = slotDuration;
                if (slot.genre.empty()) slot.genre = room.genre;
                room.djSlots.push_back(slot);
            }
        }
    }
    spdlog::info("EventPlanningServiceExtensions: Assigned {} DJs to rooms", djIdx);
}

void EventPlanningServiceExtensions::assignTracksToRoom(RoomConfig& room, const std::vector<Models::Track>& trackPool) {
    for (auto& slot : room.djSlots) {
        double targetSeconds = slot.durationMinutes * 60.0;
        double totalDuration = 0.0;

        for (const auto& track : trackPool) {
            if (totalDuration >= targetSeconds) break;
            if (!slot.genre.empty() && track.genre != slot.genre) continue;

            float energyDiff = std::abs(track.energy - slot.energyTarget);
            if (energyDiff > 4.0f) continue;

            slot.tracks.push_back(track);
            totalDuration += track.duration;
        }
    }
}

TechRider EventPlanningServiceExtensions::generateTechRider(const Models::EventPlan& event, const MultiRoomPlan& plan) {
    TechRider rider;
    rider.eventName = event.name;

    for (const auto& room : plan.rooms) {
        rider.items.push_back({"audio", "DJ Controller/CDJ Setup - " + room.roomName, 1, true, ""});
        rider.items.push_back({"audio", "DJ Mixer - " + room.roomName, 1, true, ""});
        rider.items.push_back({"audio", "Monitor Speakers - " + room.roomName, 2, true, ""});
        rider.items.push_back({"audio", "PA System - " + room.roomName, 1, true,
                               room.soundSystem.empty() ? "Adequate for " + std::to_string(room.capacity) + " people" : room.soundSystem});
        rider.items.push_back({"audio", "Headphones - " + room.roomName, 2, false, "Backup pair"});

        rider.items.push_back({"lighting", "LED Moving Heads - " + room.roomName, 4, false, ""});
        rider.items.push_back({"lighting", "Strobe Light - " + room.roomName, 2, false, ""});
    }

    rider.soundSystemRequirements = event.soundSystem;
    rider.djBoothRequirements = event.djBooth;
    rider.lightingRequirements = "Standard club lighting per room";

    spdlog::info("EventPlanningServiceExtensions: Tech rider generated - {} items", rider.items.size());
    return rider;
}

std::string EventPlanningServiceExtensions::generateRunSheet(const MultiRoomPlan& plan) {
    std::ostringstream ss;
    ss << "=== RUN SHEET: " << plan.eventName << " ===\n\n";

    for (const auto& room : plan.rooms) {
        ss << "--- " << room.roomName << " ---\n";
        ss << "Genre: " << room.genre << "\n";
        ss << "Capacity: " << room.capacity << "\n\n";

        for (size_t i = 0; i < room.djSlots.size(); ++i) {
            const auto& slot = room.djSlots[i];
            ss << "  " << (i + 1) << ". " << slot.djName;
            ss << " | " << slot.startTime << " - " << slot.endTime;
            ss << " (" << static_cast<int>(slot.durationMinutes) << " min)";
            ss << " | Genre: " << slot.genre;
            ss << " | Energy: " << std::fixed << std::setprecision(1) << slot.energyTarget;
            if (!slot.notes.empty()) ss << " | Note: " << slot.notes;
            ss << "\n";
        }
        ss << "\n";
    }

    ss << "Total Duration: " << static_cast<int>(plan.totalDuration) << " min\n";
    ss << "Total DJs: " << plan.totalDJs << "\n";
    return ss.str();
}

std::string EventPlanningServiceExtensions::generateTimeline(const Models::EventPlan& event) {
    std::ostringstream ss;
    ss << "TIMELINE: " << event.name << "\n";
    ss << "Date: " << event.date << "\n";
    ss << "Venue: " << event.venue << "\n";
    ss << event.startTime << " - " << event.endTime << "\n\n";

    for (size_t i = 0; i < event.sections.size(); ++i) {
        const auto& section = event.sections[i];
        ss << "[" << section.startTime << "] " << section.name;
        ss << " (" << static_cast<int>(section.duration) << " min)";
        if (!section.genre.empty()) ss << " | " << section.genre;
        ss << " | Energy: " << std::fixed << std::setprecision(1) << section.energy;
        ss << "\n";
    }
    return ss.str();
}

std::map<std::string, double> EventPlanningServiceExtensions::calculateBudget(
    const MultiRoomPlan& plan, double djHourlyRate, double venueHourlyRate) {

    std::map<std::string, double> budget;
    double totalDJHours = 0.0;

    for (const auto& room : plan.rooms) {
        for (const auto& slot : room.djSlots) {
            totalDJHours += slot.durationMinutes / 60.0;
        }
    }

    budget["dj_fees"] = totalDJHours * djHourlyRate;
    budget["venue"] = (plan.totalDuration / 60.0) * venueHourlyRate;
    budget["sound_equipment"] = static_cast<double>(plan.rooms.size()) * 500.0;
    budget["lighting"] = static_cast<double>(plan.rooms.size()) * 300.0;
    budget["misc"] = (budget["dj_fees"] + budget["venue"]) * 0.1;
    budget["total"] = budget["dj_fees"] + budget["venue"] + budget["sound_equipment"] +
                      budget["lighting"] + budget["misc"];

    spdlog::info("EventPlanningServiceExtensions: Budget calculated - total={:.2f}", budget["total"]);
    return budget;
}

double EventPlanningServiceExtensions::parseTime(const std::string& timeStr) const {
    if (timeStr.size() < 3) return 0.0;
    try {
        size_t sep = timeStr.find(':');
        if (sep == std::string::npos) return 0.0;
        int hours = std::stoi(timeStr.substr(0, sep));
        int minutes = std::stoi(timeStr.substr(sep + 1));
        return hours * 60.0 + minutes;
    } catch (...) { return 0.0; }
}

} // namespace BeatMate::Services::Preparation
