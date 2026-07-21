#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace BeatMate::Services::Preparation {
struct EventPlan {
    int64_t id = 0;
    std::string name;
    std::string venue;
    int64_t startTime = 0;      // unix seconds (heure locale saisie)
    int64_t endTime = 0;        // unix seconds
    std::string style;
    std::string notes;
    std::string city;
    std::string address;
    double      fee = 0.0;
    std::string currency = "EUR";
    std::string status = "confirmed";  // planned | confirmed | completed | cancelled
    std::string reminders;             // CSV minutes ("60,10080"), vide = réglage global
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(EventPlan, id, name, venue, startTime, endTime,
                                                style, notes, city, address, fee, currency, status,
                                                reminders) };

class EventPlanner {
public:
    EventPlanner();
    int64_t createEvent(const EventPlan& plan);
    bool updateEvent(const EventPlan& plan);
    bool deleteEvent(int64_t id);
    std::vector<EventPlan> getEvents();
    std::optional<EventPlan> getEvent(int64_t id);
private:
    void load();
    void save();
    std::vector<EventPlan> events_;
    int64_t nextId_ = 1;
};
} // namespace BeatMate::Services::Preparation
