#pragma once
#include <string>
#include <utility>
#include <vector>

namespace BeatMate::Services::Preparation {
struct EnergyCurve { std::vector<float> values; double durationMinutes = 60.0; std::string style; };
enum class EnergyTemplate { WarmUp, PeakTime, AfterHours, Festival, Lounge, Progressive };

class EnergyProfileGen {
public:
    EnergyProfileGen() = default;
    EnergyCurve generateProfile(double durationMinutes, EnergyTemplate style);
    EnergyCurve generateCustomProfile(double durationMinutes, const std::vector<std::pair<float, float>>& keypoints);
    static std::string templateName(EnergyTemplate t);
};
} // namespace BeatMate::Services::Preparation
