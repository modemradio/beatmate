#pragma once
#include "AutomationLane.h"
#include <functional>
#include <memory>
#include <vector>
namespace BeatMate::Core {
using AutomationApplyCallback = std::function<void(const std::string& param, double value)>;
class AutomationPlayback {
public:
    AutomationPlayback() = default;
    void addLane(std::unique_ptr<AutomationLane> lane) { lanes_.push_back(std::move(lane)); }
    void play(double currentTime, AutomationApplyCallback callback);
    void clear() { lanes_.clear(); }
private:
    std::vector<std::unique_ptr<AutomationLane>> lanes_;
};
} // namespace BeatMate::Core
