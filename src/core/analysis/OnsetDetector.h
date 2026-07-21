#pragma once

#include <vector>

namespace BeatMate::Core {

class AudioTrack;

class OnsetDetector {
public:
    OnsetDetector();
    ~OnsetDetector();

    // Returns onset positions in seconds
    std::vector<double> detect(const AudioTrack& track);

    void setThreshold(float thresh) { threshold_ = thresh; }
    void setMinInterval(double seconds) { minInterval_ = seconds; }

private:
    float threshold_ = 0.5f;
    double minInterval_ = 0.05; // 50ms minimum between onsets
};

} // namespace BeatMate::Core
