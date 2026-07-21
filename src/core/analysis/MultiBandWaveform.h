#pragma once

#include <vector>

namespace BeatMate::Core {

class AudioTrack;

struct MultiBandWaveformData {
    std::vector<float> low;    // Bass (red)
    std::vector<float> mid;    // Mids (green)
    std::vector<float> high;   // Treble (blue)
    int resolution = 0;
};

class MultiBandWaveform {
public:
    MultiBandWaveform();
    ~MultiBandWaveform();

    MultiBandWaveformData generate(const AudioTrack& track, int numPoints = 800);

    void setLowCrossover(float freq) { lowCrossover_ = freq; }
    void setHighCrossover(float freq) { highCrossover_ = freq; }

private:
    float lowCrossover_ = 200.0f;
    float highCrossover_ = 4000.0f;
};

} // namespace BeatMate::Core
