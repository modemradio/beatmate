#pragma once
#include <string>
#include <vector>

namespace BeatMate::Services::Preparation {
struct TimelineSection { int position = 0; std::string name; double startTime = 0.0; double duration = 0.0; float targetEnergy = 5.0f; std::string style; };

class EventTimeline {
public:
    EventTimeline() = default;
    void addSection(const TimelineSection& section);
    void removeSection(int position);
    void reorder(const std::vector<int>& newOrder);
    std::vector<TimelineSection> getSections() const { return sections_; }
    double getTotalDuration() const;
private:
    std::vector<TimelineSection> sections_;
};
} // namespace BeatMate::Services::Preparation
