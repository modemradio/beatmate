#pragma once
#include <mutex>
#include <string>
#include <vector>
namespace BeatMate::Core {
enum class CurveType { Linear, Bezier, Step, SCurve };
struct AutomationPoint { double time; double value; CurveType curve = CurveType::Linear; };
class AutomationLane {
public:
    AutomationLane(const std::string& paramName = "");
    ~AutomationLane();
    void addPoint(double time, double value, CurveType curve = CurveType::Linear);
    void removePoint(double time, double tolerance = 0.01);
    double getValueAt(double time) const;
    void clear();
    const std::vector<AutomationPoint>& getPoints() const { return points_; }
    std::string getParameterName() const { return paramName_; }
private:
    std::vector<AutomationPoint> points_;
    std::string paramName_;
    mutable std::mutex mutex_;
    double interpolate(const AutomationPoint& a, const AutomationPoint& b, double t) const;
};
} // namespace BeatMate::Core
