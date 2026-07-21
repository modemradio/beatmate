#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

struct SpectrumData {
    int64_t trackId = 0;

    std::vector<float> magnitudes;
    std::vector<float> phases;
    std::vector<float> frequencies;  // bin centers in Hz

    int fftSize = 2048;
    int sampleRate = 44100;

    SpectrumData() = default;
    explicit SpectrumData(int64_t trackId) : trackId(trackId) {}

    SpectrumData(int64_t trackId, int fftSize, int sampleRate)
        : trackId(trackId), fftSize(fftSize), sampleRate(sampleRate) {}

    bool operator==(const SpectrumData& other) const { return trackId == other.trackId; }

    [[nodiscard]] float nyquistFrequency() const {
        return static_cast<float>(sampleRate) / 2.0f;
    }

    [[nodiscard]] float frequencyResolution() const {
        return static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SpectrumData,
        trackId, magnitudes, phases, frequencies,
        fftSize, sampleRate
    )
};

} // namespace BeatMate::Models
