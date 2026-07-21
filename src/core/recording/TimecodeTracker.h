#pragma once
#include <atomic>
#include <string>
namespace BeatMate::Core {
class TimecodeTracker {
public:
    TimecodeTracker() = default;
    void setPosition(double seconds) { position_.store(seconds); }
    double getPosition() const { return position_.load(); }
    void advance(double seconds) { position_.store(position_.load() + seconds); }
    void reset() { position_.store(0.0); }
    std::string getTimecodeString() const;
private:
    std::atomic<double> position_{0.0};
};
}
