#pragma once
#include "AutomationLane.h"
namespace BeatMate::Core {
class TempoAutomation {
public:
    TempoAutomation() : lane_("tempo") {}
    void setTempo(double time, double bpm) { lane_.addPoint(time, bpm); }
    double getTempoAt(double time) const { return lane_.getValueAt(time); }
    void clear() { lane_.clear(); }
    double getPositionInBeats(double timeSec) const;
private:
    AutomationLane lane_;
};
} // namespace BeatMate::Core
