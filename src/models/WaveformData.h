#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

struct WaveformData {
    int64_t trackId = 0;

    std::vector<float> peaks;       // peak values per sample window
    std::vector<float> rms;         // RMS values per sample window

    // Frequency band separation (for colored waveform display)
    std::vector<float> lowBand;     // low frequency band (bass)
    std::vector<float> midBand;     // mid frequency band
    std::vector<float> highBand;    // high frequency band

    int sampleRate = 44100;
    int resolution = 256;           // samples per waveform point
    double duration = 0.0;          // total duration in seconds

    // Overview (low resolution for minimap/scrollbar)
    std::vector<float> overviewPeaks;
    std::vector<float> overviewLow;
    std::vector<float> overviewMid;
    std::vector<float> overviewHigh;
    int overviewResolution = 2048;  // samples per overview point

    WaveformData() = default;
    explicit WaveformData(int64_t trackId) : trackId(trackId) {}

    bool operator==(const WaveformData& other) const { return trackId == other.trackId; }

    [[nodiscard]] size_t pointCount() const { return peaks.size(); }
    [[nodiscard]] size_t overviewPointCount() const { return overviewPeaks.size(); }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(WaveformData,
        trackId, peaks, rms,
        lowBand, midBand, highBand,
        sampleRate, resolution, duration,
        overviewPeaks, overviewLow, overviewMid, overviewHigh,
        overviewResolution
    )
};

} // namespace BeatMate::Models
