#include "AutomationPlayback.h"

namespace BeatMate::Core {

void AutomationPlayback::play(double currentTime, AutomationApplyCallback callback) {
    if (!callback) return;
    for (auto& lane : lanes_) {
        double value = lane->getValueAt(currentTime);
        callback(lane->getParameterName(), value);
    }
}

}
