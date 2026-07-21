#pragma once

#include "../audio/AudioTrack.h"
#include <string>
#include <vector>

namespace BeatMate::Core {

class WaveformGenerator {
public:
    WaveformGenerator();
    ~WaveformGenerator();

    WaveformData generate(const AudioTrack& track, int resolution = 800);
    WaveformData generateDetailed(const AudioTrack& track, int resolution = 4000);

    bool saveToCache(const std::string& trackId, const WaveformData& data,
                     const std::string& cacheDir = "WaveformCache");
    bool loadFromCache(const std::string& trackId, WaveformData& data,
                       const std::string& cacheDir = "WaveformCache");

private:
    WaveformData computeWaveform(const AudioTrack& track, int numPoints);
};

} // namespace BeatMate::Core
