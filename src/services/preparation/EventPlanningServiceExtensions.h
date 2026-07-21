#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include "../../models/EventPlan.h"
#include "../../models/Track.h"

namespace BeatMate::Services::Preparation {

struct DJSlot {
    std::string djName;
    std::string startTime;
    std::string endTime;
    double durationMinutes = 0.0;
    std::string genre;
    float energyTarget = 5.0f;
    std::vector<Models::Track> tracks;
    std::string notes;
};

struct RoomConfig {
    std::string roomName;
    std::string genre;
    int capacity = 0;
    std::string soundSystem;
    std::vector<DJSlot> djSlots;
};

struct MultiRoomPlan {
    std::string eventName;
    std::vector<RoomConfig> rooms;
    double totalDuration = 0.0;
    int totalDJs = 0;
    int totalTracks = 0;
};

struct TechRiderItem {
    std::string category;   // "audio", "lighting", "visual", "other"
    std::string item;
    int quantity = 1;
    bool required = true;
    std::string notes;
};

struct TechRider {
    std::string eventName;
    std::vector<TechRiderItem> items;
    std::string soundSystemRequirements;
    std::string djBoothRequirements;
    std::string lightingRequirements;
};

class EventPlanningServiceExtensions {
public:
    EventPlanningServiceExtensions() = default;

    MultiRoomPlan planMultiRoom(const Models::EventPlan& event, const std::vector<RoomConfig>& roomConfigs);
    void assignDJsToSlots(MultiRoomPlan& plan, const std::vector<DJSlot>& djPool);
    void assignTracksToRoom(RoomConfig& room, const std::vector<Models::Track>& trackPool);

    TechRider generateTechRider(const Models::EventPlan& event, const MultiRoomPlan& plan);
    std::string generateRunSheet(const MultiRoomPlan& plan);
    std::string generateTimeline(const Models::EventPlan& event);

    std::map<std::string, double> calculateBudget(const MultiRoomPlan& plan,
                                                    double djHourlyRate, double venueHourlyRate);

private:
    double parseTime(const std::string& timeStr) const;
};

} // namespace BeatMate::Services::Preparation
