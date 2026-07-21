#include "EventTimeline.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <numeric>

namespace BeatMate::Services::Preparation {

void EventTimeline::addSection(const TimelineSection& section) {
    sections_.push_back(section);
    spdlog::debug("EventTimeline: Added section '{}' at position {}", section.name, section.position);
}

void EventTimeline::removeSection(int position) {
    sections_.erase(std::remove_if(sections_.begin(), sections_.end(),
        [position](const auto& s) { return s.position == position; }), sections_.end());
}

void EventTimeline::reorder(const std::vector<int>& newOrder) {
    std::vector<TimelineSection> reordered;
    for (int idx : newOrder) {
        if (idx >= 0 && idx < static_cast<int>(sections_.size())) {
            reordered.push_back(sections_[static_cast<size_t>(idx)]);
        }
    }
    sections_ = reordered;
}

double EventTimeline::getTotalDuration() const {
    double total = 0;
    for (const auto& s : sections_) total += s.duration;
    return total;
}

}
