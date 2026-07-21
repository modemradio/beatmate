#include "MultiBandWaveform.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

MultiBandWaveform::MultiBandWaveform() = default;
MultiBandWaveform::~MultiBandWaveform() = default;

MultiBandWaveformData MultiBandWaveform::generate(const AudioTrack& track, int numPoints) {
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t totalSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    MultiBandWaveformData result;
    result.resolution = static_cast<int>(totalSamples / numPoints);
    result.low.resize(numPoints, 0.0f);
    result.mid.resize(numPoints, 0.0f);
    result.high.resize(numPoints, 0.0f);

    int fftSize = 1024;
    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    int lowBinMax = static_cast<int>(lowCrossover_ * fftSize / sr);
    int highBinMin = static_cast<int>(highCrossover_ * fftSize / sr);

    size_t samplesPerPoint = totalSamples / numPoints;

    for (int p = 0; p < numPoints; ++p) {
        size_t offset = p * samplesPerPoint;
        if (offset + fftSize > totalSamples) break;

        std::vector<std::complex<float>> spectrum;
        fft.forward(data + offset, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        float lowEnergy = 0, midEnergy = 0, highEnergy = 0;
        for (int bin = 1; bin < static_cast<int>(mag.size()); ++bin) {
            float m = mag[bin] * mag[bin];
            if (bin <= lowBinMax) {
                lowEnergy += m;
            } else if (bin >= highBinMin) {
                highEnergy += m;
            } else {
                midEnergy += m;
            }
        }

        result.low[p] = std::sqrt(lowEnergy);
        result.mid[p] = std::sqrt(midEnergy);
        result.high[p] = std::sqrt(highEnergy);
    }

    auto normalize = [](std::vector<float>& v) {
        float maxVal = *std::max_element(v.begin(), v.end());
        if (maxVal > 0) for (auto& x : v) x /= maxVal;
    };

    normalize(result.low);
    normalize(result.mid);
    normalize(result.high);

    spdlog::info("MultiBandWaveform: generated {} points", numPoints);
    return result;
}

} // namespace BeatMate::Core
