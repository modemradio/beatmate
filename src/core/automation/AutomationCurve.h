#pragma once
#include <vector>
namespace BeatMate::Core {
struct ControlPoint { double x, y; double cx1, cy1, cx2, cy2; };
class AutomationCurve {
public:
    AutomationCurve() = default;
    void addControlPoint(double x, double y, double cx1 = 0, double cy1 = 0, double cx2 = 0, double cy2 = 0);
    double evaluate(double t) const;
    void clear() { points_.clear(); }
    int getPointCount() const { return static_cast<int>(points_.size()); }
private:
    std::vector<ControlPoint> points_;
    static double cubicBezier(double t, double p0, double p1, double p2, double p3);
};
}
