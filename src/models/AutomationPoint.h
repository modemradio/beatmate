#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

enum class CurveType : int {
    Linear = 0,
    Bezier = 1,
    Step = 2,
    SCurve = 3
};

NLOHMANN_JSON_SERIALIZE_ENUM(CurveType, {
    { CurveType::Linear, "Linear" },
    { CurveType::Bezier, "Bezier" },
    { CurveType::Step, "Step" },
    { CurveType::SCurve, "SCurve" }
})

struct ControlPoint {
    double x = 0.0;
    double y = 0.0;

    ControlPoint() = default;
    ControlPoint(double x, double y) : x(x), y(y) {}

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ControlPoint, x, y)
};

struct AutomationPoint {
    double time = 0.0;         // time in seconds
    float value = 0.0f;        // 0-1 normalized value
    CurveType curveType = CurveType::Linear;

    // Bezier control points (used when curveType == Bezier)
    ControlPoint controlPoint1;
    ControlPoint controlPoint2;

    AutomationPoint() = default;

    AutomationPoint(double time, float value)
        : time(time), value(value) {}

    AutomationPoint(double time, float value, CurveType curveType)
        : time(time), value(value), curveType(curveType) {}

    AutomationPoint(double time, float value, CurveType curveType,
                    const ControlPoint& cp1, const ControlPoint& cp2)
        : time(time), value(value), curveType(curveType),
          controlPoint1(cp1), controlPoint2(cp2) {}

    bool operator==(const AutomationPoint& other) const {
        return time == other.time && value == other.value;
    }

    bool operator<(const AutomationPoint& other) const {
        return time < other.time;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AutomationPoint,
        time, value, curveType, controlPoint1, controlPoint2
    )
};

struct AutomationLane {
    std::string parameterName;      // e.g. "volume", "lowEQ", "filter"
    std::vector<AutomationPoint> points;
    float defaultValue = 0.5f;
    float minValue = 0.0f;
    float maxValue = 1.0f;

    AutomationLane() = default;

    explicit AutomationLane(const std::string& parameterName)
        : parameterName(parameterName) {}

    [[nodiscard]] float valueAtTime(double time) const {
        if (points.empty()) return defaultValue;
        if (time <= points.front().time) return points.front().value;
        if (time >= points.back().time) return points.back().value;

        for (size_t i = 0; i + 1 < points.size(); ++i) {
            if (time >= points[i].time && time < points[i + 1].time) {
                if (points[i].curveType == CurveType::Step) {
                    return points[i].value;
                }
                double t = (time - points[i].time) / (points[i + 1].time - points[i].time);
                return points[i].value + static_cast<float>(t) * (points[i + 1].value - points[i].value);
            }
        }
        return defaultValue;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AutomationLane,
        parameterName, points, defaultValue, minValue, maxValue
    )
};

} // namespace BeatMate::Models
