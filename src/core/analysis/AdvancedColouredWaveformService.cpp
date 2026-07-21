#include "AdvancedColouredWaveformService.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

AdvancedColouredWaveformService::AdvancedColouredWaveformService() = default;
AdvancedColouredWaveformService::~AdvancedColouredWaveformService() = default;

void AdvancedColouredWaveformService::compute3BandEnergy(const float* data, int numSamples,
                                                           int sampleRate,
                                                           float& low, float& mid, float& high) {
    low = mid = high = 0.0f;

    if (numSamples < 256) {
        float rms = 0.0f;
        for (int i = 0; i < numSamples; ++i) rms += data[i] * data[i];
        low = mid = high = std::sqrt(rms / numSamples);
        return;
    }

    int fftSize = 256;
    while (fftSize < numSamples && fftSize < 2048) fftSize *= 2;
    if (fftSize > numSamples) fftSize = numSamples;

    int actualFFT = 256;
    while (actualFFT * 2 <= fftSize) actualFFT *= 2;

    FFTProcessor fft(actualFFT);
    fft.setWindow(WindowType::Hann);

    int numWindows = numSamples / actualFFT;
    if (numWindows == 0) numWindows = 1;

    float lowSum = 0.0f, midSum = 0.0f, highSum = 0.0f;

    for (int w = 0; w < numWindows; ++w) {
        int offset = w * actualFFT;
        if (offset + actualFFT > numSamples) break;

        std::vector<std::complex<float>> spectrum;
        fft.forward(data + offset, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        int lowBin = static_cast<int>(lowCrossover_ * actualFFT / sampleRate);
        int highBin = static_cast<int>(highCrossover_ * actualFFT / sampleRate);

        for (int bin = 1; bin < static_cast<int>(mag.size()); ++bin) {
            float energy = mag[bin] * mag[bin];
            if (bin <= lowBin) lowSum += energy;
            else if (bin <= highBin) midSum += energy;
            else highSum += energy;
        }
    }

    float scale = 1.0f / numWindows;
    low = std::sqrt(lowSum * scale);
    mid = std::sqrt(midSum * scale);
    high = std::sqrt(highSum * scale);
}

WaveformColor AdvancedColouredWaveformService::mapToColor(float low, float mid, float high) {
    float total = low + mid + high;
    if (total < 1e-10f) return {64, 64, 64};

    float lr = low / total;
    float mr = mid / total;
    float hr = high / total;

    uint8_t r = static_cast<uint8_t>(std::clamp(
        lowColor_.r * lr + midColor_.r * mr + highColor_.r * hr, 0.0f, 255.0f));
    uint8_t g = static_cast<uint8_t>(std::clamp(
        lowColor_.g * lr + midColor_.g * mr + highColor_.g * hr, 0.0f, 255.0f));
    uint8_t b = static_cast<uint8_t>(std::clamp(
        lowColor_.b * lr + midColor_.b * mr + highColor_.b * hr, 0.0f, 255.0f));

    float brightness = std::clamp(total * 3.0f, 0.3f, 1.0f);
    r = static_cast<uint8_t>(r * brightness);
    g = static_cast<uint8_t>(g * brightness);
    b = static_cast<uint8_t>(b * brightness);

    return {r, g, b};
}

ColouredWaveformData AdvancedColouredWaveformService::computeWaveform(
    const AudioTrack& track, int numPoints) {

    ColouredWaveformData result;
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t totalSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    if (totalSamples == 0 || numPoints == 0) return result;

    result.sampleRate = sr;
    result.duration = track.getDuration();
    result.resolution = static_cast<int>(totalSamples / numPoints);

    size_t samplesPerPoint = totalSamples / numPoints;
    result.points.resize(numPoints);

    for (int i = 0; i < numPoints; ++i) {
        size_t start = i * samplesPerPoint;
        size_t end = std::min(start + samplesPerPoint, totalSamples);
        int segLen = static_cast<int>(end - start);

        if (segLen <= 0) continue;

        float peak = 0.0f;
        for (size_t s = start; s < end; ++s) {
            peak = std::max(peak, std::fabs(data[s]));
        }
        result.points[i].amplitude = peak;

        compute3BandEnergy(data + start, segLen, sr,
                           result.points[i].low, result.points[i].mid, result.points[i].high);

        result.points[i].color = mapToColor(result.points[i].low, result.points[i].mid,
                                             result.points[i].high);
    }

    float maxLow = 0.0f, maxMid = 0.0f, maxHigh = 0.0f;
    for (auto& p : result.points) {
        maxLow = std::max(maxLow, p.low);
        maxMid = std::max(maxMid, p.mid);
        maxHigh = std::max(maxHigh, p.high);
    }
    if (maxLow > 0) for (auto& p : result.points) p.low /= maxLow;
    if (maxMid > 0) for (auto& p : result.points) p.mid /= maxMid;
    if (maxHigh > 0) for (auto& p : result.points) p.high /= maxHigh;

    return result;
}

ColouredWaveformData AdvancedColouredWaveformService::generate(const AudioTrack& track, int numPoints) {
    spdlog::info("AdvancedColouredWaveformService: generating overview ({} points)", numPoints);
    return computeWaveform(track, numPoints);
}

ColouredWaveformData AdvancedColouredWaveformService::generateDetailed(const AudioTrack& track,
                                                                        int numPoints) {
    spdlog::info("AdvancedColouredWaveformService: generating detailed ({} points)", numPoints);
    auto result = computeWaveform(track, numPoints);
    result.detailedResolution = result.resolution;
    result.detailedPoints = result.points;
    return result;
}

}
