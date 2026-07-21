#pragma once
#include <functional>
#include <string>
#include <vector>
#include <juce_core/juce_core.h>

namespace BeatMate::Core {

struct RgbPeaksData {
    double duration = 0.0;
    std::vector<float> peaks;
    std::vector<float> bass;
    std::vector<float> mid;
    std::vector<float> treble;
    bool valid() const { return !peaks.empty() && duration > 0.0; }
};

class RgbPeaksGenerator {
public:
    static juce::File cacheDirectory();
    static juce::File cacheFileFor(const std::string& audioPath);
    static bool isCacheValid(const std::string& audioPath);
    static bool read(const juce::File& cacheFile, RgbPeaksData& out);
    static bool write(const juce::File& cacheFile, const RgbPeaksData& data);

    RgbPeaksData generate(const std::string& audioPath,
                          const std::function<bool()>& shouldAbort,
                          const std::function<void(const RgbPeaksData&)>& onPartial);
};

} // namespace BeatMate::Core
