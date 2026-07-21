#pragma once

#include <vector>

namespace BeatMate::Core {

enum class SRCQuality { Fast, Medium, High };

class SampleRateConverter {
public:
    SampleRateConverter();
    ~SampleRateConverter();

    std::vector<float> convert(const float* data, size_t numFrames, int channels,
                               int fromRate, int toRate,
                               SRCQuality quality = SRCQuality::High);

    std::vector<float> convert(const std::vector<float>& data, int channels,
                               int fromRate, int toRate,
                               SRCQuality quality = SRCQuality::High);

private:
    std::vector<float> convertLinear(const float* data, size_t numFrames,
                                     int channels, double ratio);

    std::vector<float> convertSinc(const float* data, size_t numFrames,
                                    int channels, double ratio, int taps = 32);

    static double sinc(double x);
    static double windowedSinc(double x, int taps);
};

} // namespace BeatMate::Core
