#include "TimecodeTracker.h"
#include <cmath>
#include <sstream>
#include <iomanip>

namespace BeatMate::Core {

std::string TimecodeTracker::getTimecodeString() const {
    double pos = position_.load();
    int hours = static_cast<int>(pos / 3600);
    int minutes = static_cast<int>(std::fmod(pos, 3600) / 60);
    int seconds = static_cast<int>(std::fmod(pos, 60));
    int millis = static_cast<int>(std::fmod(pos * 1000, 1000));

    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << hours << ":"
        << std::setw(2) << minutes << ":"
        << std::setw(2) << seconds << "."
        << std::setw(3) << millis;
    return oss.str();
}

}
