#pragma once

#include <memory>
#include <string>
#include <vector>

namespace BeatMate::Core {

class AudioTrack;

struct KeyResult {
    std::string key;          // e.g. "Am", "C", "F#m"
    std::string camelotKey;   // e.g. "8A", "1B"
    double confidence = 0.0;  // 0 to 1
    bool isMinor = false;
    int pitchClass = 0;       // 0-11 (C=0, C#=1, D=2, ...)
};

class KeyDetector {
public:
    KeyDetector();
    ~KeyDetector();

    KeyResult detect(const AudioTrack& track);

private:
    std::vector<double> extractChromagram(const AudioTrack& track);

    // Krumhansl-Schmuckler key profiles
    struct KeyProfile {
        double major[12];
        double minor[12];
    };
    static KeyProfile getKrumhanslProfile();

    KeyResult matchKey(const std::vector<double>& chromagram);

    static std::string toCamelotKey(int pitchClass, bool isMinor);
    static std::string toKeyName(int pitchClass, bool isMinor);
};

} // namespace BeatMate::Core
