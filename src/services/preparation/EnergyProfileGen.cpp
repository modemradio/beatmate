#include "EnergyProfileGen.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Services::Preparation {

EnergyCurve EnergyProfileGen::generateProfile(double durationMinutes, EnergyTemplate style) {
    EnergyCurve curve;
    curve.durationMinutes = durationMinutes;
    curve.style = templateName(style);
    int points = static_cast<int>(durationMinutes);
    curve.values.resize(static_cast<size_t>(points));

    for (int i = 0; i < points; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(points);
        switch (style) {
            case EnergyTemplate::WarmUp: curve.values[i] = 2.0f + 6.0f * t; break;
            case EnergyTemplate::PeakTime: curve.values[i] = 7.0f + 3.0f * std::sin(t * 3.14159f); break;
            case EnergyTemplate::AfterHours: curve.values[i] = 8.0f - 5.0f * t; break;
            case EnergyTemplate::Festival: curve.values[i] = 5.0f + 5.0f * std::sin(t * 6.28318f * 2); break;
            case EnergyTemplate::Lounge: curve.values[i] = 3.0f + 2.0f * std::sin(t * 3.14159f); break;
            case EnergyTemplate::Progressive: curve.values[i] = 3.0f + 7.0f * (1.0f - std::exp(-3.0f * t)); break;
        }
        curve.values[i] = std::clamp(curve.values[i], 1.0f, 10.0f);
    }

    spdlog::info("EnergyProfileGen: Generated '{}' profile, {:.0f} minutes", curve.style, durationMinutes);
    return curve;
}

EnergyCurve EnergyProfileGen::generateCustomProfile(double durationMinutes,
    const std::vector<std::pair<float, float>>& keypoints) {
    EnergyCurve curve;
    curve.durationMinutes = durationMinutes;
    curve.style = "Custom";
    int points = static_cast<int>(durationMinutes);
    curve.values.resize(static_cast<size_t>(points));

    for (int i = 0; i < points; ++i) {
        float t = static_cast<float>(i) / points;
        float val = 5.0f;
        for (size_t k = 0; k + 1 < keypoints.size(); ++k) {
            if (t >= keypoints[k].first && t <= keypoints[k + 1].first) {
                float kt = (t - keypoints[k].first) / (keypoints[k + 1].first - keypoints[k].first);
                val = keypoints[k].second + kt * (keypoints[k + 1].second - keypoints[k].second);
                break;
            }
        }
        curve.values[i] = std::clamp(val, 1.0f, 10.0f);
    }
    return curve;
}

std::string EnergyProfileGen::templateName(EnergyTemplate t) {
    switch (t) {
        case EnergyTemplate::WarmUp: return "Warm-Up";
        case EnergyTemplate::PeakTime: return "Peak-Time";
        case EnergyTemplate::AfterHours: return "After-Hours";
        case EnergyTemplate::Festival: return "Festival";
        case EnergyTemplate::Lounge: return "Lounge";
        case EnergyTemplate::Progressive: return "Progressive";
        default: return "Unknown";
    }
}

} // namespace BeatMate::Services::Preparation
