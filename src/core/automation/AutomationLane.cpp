#include "AutomationLane.h"
#include <algorithm>
#include <cmath>

namespace BeatMate::Core {

AutomationLane::AutomationLane(const std::string& paramName) : paramName_(paramName) {}
AutomationLane::~AutomationLane() = default;

void AutomationLane::addPoint(double time, double value, CurveType curve) {
    std::lock_guard<std::mutex> lock(mutex_);
    AutomationPoint p{time, value, curve};
    auto it = std::lower_bound(points_.begin(), points_.end(), p,
        [](const AutomationPoint& a, const AutomationPoint& b) { return a.time < b.time; });
    points_.insert(it, p);
}

void AutomationLane::removePoint(double time, double tolerance) {
    std::lock_guard<std::mutex> lock(mutex_);
    points_.erase(std::remove_if(points_.begin(), points_.end(),
        [time, tolerance](const AutomationPoint& p) {
            return std::fabs(p.time - time) < tolerance;
        }), points_.end());
}

double AutomationLane::interpolate(const AutomationPoint& a, const AutomationPoint& b, double t) const {
    double frac = (t - a.time) / (b.time - a.time);
    frac = std::clamp(frac, 0.0, 1.0);

    switch (a.curve) {
        case CurveType::Linear:
            return a.value + (b.value - a.value) * frac;
        case CurveType::Bezier: {
            double t2 = frac * frac;
            double t3 = t2 * frac;
            double smooth = 3.0 * t2 - 2.0 * t3;
            return a.value + (b.value - a.value) * smooth;
        }
        case CurveType::Step:
            return a.value; // Hold until next point
        case CurveType::SCurve: {
            double s = 0.5 * (1.0 - std::cos(frac * 3.14159265));
            return a.value + (b.value - a.value) * s;
        }
    }
    return a.value;
}

double AutomationLane::getValueAt(double time) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (points_.empty()) return 0.0;
    if (time <= points_.front().time) return points_.front().value;
    if (time >= points_.back().time) return points_.back().value;

    for (size_t i = 0; i + 1 < points_.size(); ++i) {
        if (time >= points_[i].time && time < points_[i + 1].time) {
            return interpolate(points_[i], points_[i + 1], time);
        }
    }
    return points_.back().value;
}

void AutomationLane::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    points_.clear();
}

} // namespace BeatMate::Core
