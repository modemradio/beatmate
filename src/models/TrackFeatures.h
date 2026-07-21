#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

struct TrackFeatures {
    int64_t trackId = 0;

    // MFCC (Mel-Frequency Cepstral Coefficients) - typically 13 coefficients
    std::vector<float> mfcc;

    // Chroma features (12 pitch classes)
    std::vector<float> chroma;

    float spectralContrast = 0.0f;
    float spectralBandwidth = 0.0f;

    std::vector<float> tempogram;

    float harmonicRatio = 0.0f;    // 0-1
    float percussiveRatio = 0.0f;  // 0-1

    TrackFeatures() = default;
    explicit TrackFeatures(int64_t trackId) : trackId(trackId) {}

    bool operator==(const TrackFeatures& other) const { return trackId == other.trackId; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(TrackFeatures,
        trackId, mfcc, chroma,
        spectralContrast, spectralBandwidth,
        tempogram, harmonicRatio, percussiveRatio
    )
};

} // namespace BeatMate::Models
