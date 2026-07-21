#include "EventPlanner.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <juce_core/juce_core.h>

namespace BeatMate::Services::Preparation {

namespace {
    juce::File eventsFile() {
        auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("BeatMate");
        dir.createDirectory();
        return dir.getChildFile("events.json");
    }
}

EventPlanner::EventPlanner() { load(); }

void EventPlanner::load() {
    auto f = eventsFile();
    if (!f.existsAsFile()) return;
    try {
        const auto txt = f.loadFileAsString().toStdString();
        if (txt.empty()) return;
        events_ = nlohmann::json::parse(txt).get<std::vector<EventPlan>>();
        for (const auto& e : events_) nextId_ = std::max(nextId_, e.id + 1);
        spdlog::info("EventPlanner: Loaded {} events", events_.size());
    } catch (const std::exception& ex) {
        spdlog::error("EventPlanner: Failed to load events: {}", ex.what());
    }
}

void EventPlanner::save() {
    try {
        const nlohmann::json j = events_;
        eventsFile().replaceWithText(juce::String(j.dump(2)));
    } catch (const std::exception& ex) {
        spdlog::error("EventPlanner: Failed to save events: {}", ex.what());
    }
}

int64_t EventPlanner::createEvent(const EventPlan& plan) {
    EventPlan newPlan = plan;
    newPlan.id = nextId_++;
    events_.push_back(newPlan);
    save();
    spdlog::info("EventPlanner: Created event '{}' (id={})", newPlan.name, newPlan.id);
    return newPlan.id;
}

bool EventPlanner::updateEvent(const EventPlan& plan) {
    auto it = std::find_if(events_.begin(), events_.end(), [&](const auto& e) { return e.id == plan.id; });
    if (it == events_.end()) return false;
    *it = plan;
    save();
    return true;
}

bool EventPlanner::deleteEvent(int64_t id) {
    events_.erase(std::remove_if(events_.begin(), events_.end(), [id](const auto& e) { return e.id == id; }), events_.end());
    save();
    return true;
}

std::vector<EventPlan> EventPlanner::getEvents() { return events_; }

std::optional<EventPlan> EventPlanner::getEvent(int64_t id) {
    auto it = std::find_if(events_.begin(), events_.end(), [id](const auto& e) { return e.id == id; });
    return (it != events_.end()) ? std::optional(*it) : std::nullopt;
}

} // namespace BeatMate::Services::Preparation
