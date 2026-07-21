#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace BeatMate::Core {

class AudioTrack;

struct WaveformColor {
    uint8_t r = 0, g = 0, b = 0, a = 255;

    WaveformColor() = default;
    WaveformColor(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}

    uint32_t toARGB() const {
        return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
               (static_cast<uint32_t>(g) << 8) | b;
    }
};

struct ColouredWaveformPoint {
    float amplitude = 0.0f;     // 0-1
    float low = 0.0f;           // Bass band energy
    float mid = 0.0f;           // Mid band energy
    float high = 0.0f;          // Treble band energy
    WaveformColor color;        // RGB color based on spectral content
};

struct ColouredWaveformData {
    std::vector<ColouredWaveformPoint> points;
    std::vector<ColouredWaveformPoint> detailedPoints;  // High-res version
    int resolution = 0;          // Samples per point (overview)
    int detailedResolution = 0;  // Samples per point (detailed)
    int sampleRate = 0;
    double duration = 0.0;
};

class AdvancedColouredWaveformService {
public:
    AdvancedColouredWaveformService();
    ~AdvancedColouredWaveformService();

    ColouredWaveformData generate(const AudioTrack& track, int numPoints = 800);
    ColouredWaveformData generateDetailed(const AudioTrack& track, int numPoints = 4000);

    void setLowCrossover(float freq) { lowCrossover_ = freq; }
    void setHighCrossover(float freq) { highCrossover_ = freq; }

    void setLowColor(uint8_t r, uint8_t g, uint8_t b) { lowColor_ = {r, g, b}; }
    void setMidColor(uint8_t r, uint8_t g, uint8_t b) { midColor_ = {r, g, b}; }
    void setHighColor(uint8_t r, uint8_t g, uint8_t b) { highColor_ = {r, g, b}; }

private:
    ColouredWaveformData computeWaveform(const AudioTrack& track, int numPoints);

    void compute3BandEnergy(const float* data, int numSamples, int sampleRate,
                             float& low, float& mid, float& high);

    WaveformColor mapToColor(float low, float mid, float high);

    float lowCrossover_ = 200.0f;
    float highCrossover_ = 4000.0f;

    WaveformColor lowColor_  = {255, 40, 40};     // Red for bass
    WaveformColor midColor_  = {40, 255, 40};      // Green for mids
    WaveformColor highColor_ = {40, 100, 255};     // Blue for treble
};

} // namespace BeatMate::Core
