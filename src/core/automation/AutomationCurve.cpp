#include "AutomationCurve.h"
#include <algorithm>
#include <cmath>

namespace BeatMate::Core {

void AutomationCurve::addControlPoint(double x, double y, double cx1, double cy1, double cx2, double cy2) {
    points_.push_back({x, y, cx1, cy1, cx2, cy2});
    std::sort(points_.begin(), points_.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
}

double AutomationCurve::cubicBezier(double t, double p0, double p1, double p2, double p3) {
    double u = 1.0 - t;
    return u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3;
}

double AutomationCurve::evaluate(double t) const {
    if (points_.empty()) return 0.0;
    if (points_.size() == 1 || t <= points_.front().x) return points_.front().y;
    if (t >= points_.back().x) return points_.back().y;

    for (size_t i = 0; i + 1 < points_.size(); ++i) {
        if (t >= points_[i].x && t < points_[i + 1].x) {
            double frac = (t - points_[i].x) / (points_[i + 1].x - points_[i].x);
            return cubicBezier(frac, points_[i].y,
                               points_[i].y + points_[i].cy2,
                               points_[i + 1].y + points_[i + 1].cy1,
                               points_[i + 1].y);
        }
    }
    return points_.back().y;
}

} // namespace BeatMate::Core
