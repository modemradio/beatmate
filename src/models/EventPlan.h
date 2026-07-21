#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

struct EventSection {
    std::string name;
    std::string startTime;
    double duration = 0.0;
    std::string genre;
    float energy = 5.0f;
    double bpmMin = 0.0;
    double bpmMax = 0.0;
    std::string notes;
    std::string color;

    EventSection() = default;

    EventSection(const std::string& name, const std::string& startTime, double duration)
        : name(name), startTime(startTime), duration(duration) {}

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(EventSection,
        name, startTime, duration, genre, energy,
        bpmMin, bpmMax, notes, color
    )
};

struct EventPlan {
    int64_t id = 0;
    std::string name;
    std::string venue;
    std::string date;
    std::string startTime;
    std::string endTime;
    std::string notes;
    std::string color;

    std::vector<EventSection> sections;

    std::string address;
    std::string city;
    std::string country;
    int expectedAttendance = 0;
    std::string contactName;
    std::string contactEmail;
    std::string contactPhone;
    std::string soundSystem;
    std::string djBooth;

    float fee = 0.0f;
    std::string currency;
    bool isPaid = false;

    std::string status;

    int64_t createdAt = 0;
    int64_t modifiedAt = 0;

    EventPlan() = default;

    EventPlan(int64_t id, const std::string& name, const std::string& venue, const std::string& date)
        : id(id), name(name), venue(venue), date(date) {}

    bool operator==(const EventPlan& other) const { return id == other.id; }

    [[nodiscard]] double totalDurationMinutes() const {
        double total = 0.0;
        for (const auto& section : sections) {
            total += section.duration;
        }
        return total;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(EventPlan,
        id, name, venue, date, startTime, endTime, notes, color,
        sections, address, city, country, expectedAttendance,
        contactName, contactEmail, contactPhone, soundSystem, djBooth,
        fee, currency, isPaid, status, createdAt, modifiedAt
    )
};

}
