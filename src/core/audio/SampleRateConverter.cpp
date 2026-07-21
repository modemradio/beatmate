#include "SampleRateConverter.h"
#include <cmath>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

static constexpr double kPi = 3.14159265358979323846;

SampleRateConverter::SampleRateConverter() = default;
SampleRateConverter::~SampleRateConverter() = default;

std::vector<float> SampleRateConverter::convert(const float* data, size_t numFrames,
                                                 int channels, int fromRate, int toRate,
                                                 SRCQuality quality) {
    if (fromRate == toRate) {
        return std::vector<float>(data, data + numFrames * channels);
    }

    double ratio = static_cast<double>(toRate) / fromRate;
    spdlog::debug("SRC: {}Hz -> {}Hz (ratio {:.4f})", fromRate, toRate, ratio);

    switch (quality) {
        case SRCQuality::Fast:
            return convertLinear(data, numFrames, channels, ratio);
        case SRCQuality::Medium:
            return convertSinc(data, numFrames, channels, ratio, 8);
        case SRCQuality::High:
        default:
            return convertSinc(data, numFrames, channels, ratio, 32);
    }
}

std::vector<float> SampleRateConverter::convert(const std::vector<float>& data,
                                                 int channels, int fromRate, int toRate,
                                                 SRCQuality quality) {
    return convert(data.data(), data.size() / channels, channels, fromRate, toRate, quality);
}

std::vector<float> SampleRateConverter::convertLinear(const float* data, size_t numFrames,
                                                       int channels, double ratio) {
    size_t outFrames = static_cast<size_t>(numFrames * ratio);
    std::vector<float> output(outFrames * channels);

    for (size_t i = 0; i < outFrames; ++i) {
        double srcPos = i / ratio;
        size_t idx0 = static_cast<size_t>(srcPos);
        size_t idx1 = std::min(idx0 + 1, numFrames - 1);
        double frac = srcPos - idx0;

        for (int ch = 0; ch < channels; ++ch) {
            float s0 = data[idx0 * channels + ch];
            float s1 = data[idx1 * channels + ch];
            output[i * channels + ch] = static_cast<float>(s0 + (s1 - s0) * frac);
        }
    }

    return output;
}

double SampleRateConverter::sinc(double x) {
    if (std::fabs(x) < 1e-10) return 1.0;
    double px = kPi * x;
    return std::sin(px) / px;
}

double SampleRateConverter::windowedSinc(double x, int taps) {
    if (std::fabs(x) > taps) return 0.0;
    // Blackman-Harris window
    double t = x / taps;
    double window = 0.35875 - 0.48829 * std::cos(kPi * (1.0 + t)) +
                    0.14128 * std::cos(2.0 * kPi * (1.0 + t)) -
                    0.01168 * std::cos(3.0 * kPi * (1.0 + t));
    return sinc(x) * window;
}

std::vector<float> SampleRateConverter::convertSinc(const float* data, size_t numFrames,
                                                     int channels, double ratio, int taps) {
    size_t outFrames = static_cast<size_t>(numFrames * ratio);
    std::vector<float> output(outFrames * channels, 0.0f);

    double filterCutoff = (ratio < 1.0) ? ratio : 1.0; // Anti-aliasing

    for (size_t i = 0; i < outFrames; ++i) {
        double srcPos = i / ratio;
        int srcCenter = static_cast<int>(std::round(srcPos));

        for (int ch = 0; ch < channels; ++ch) {
            double sum = 0.0;
            double weightSum = 0.0;

            for (int j = -taps; j <= taps; ++j) {
                int srcIdx = srcCenter + j;
                if (srcIdx < 0 || srcIdx >= static_cast<int>(numFrames)) continue;

                double delta = srcPos - srcIdx;
                double weight = windowedSinc(delta * filterCutoff, taps);
                sum += data[srcIdx * channels + ch] * weight;
                weightSum += weight;
            }

            if (weightSum > 0.0) {
                output[i * channels + ch] = static_cast<float>(sum / weightSum);
            }
        }
    }

    return output;
}

} // namespace BeatMate::Core
