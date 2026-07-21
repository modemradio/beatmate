#include "EnergyDetector.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

float EnergyDetector::computeRMS(const float* data, size_t numSamples)
{
    if (numSamples == 0) return 0.0f;
    double sum = 0.0;
    for (size_t i = 0; i < numSamples; ++i)
        sum += data[i] * data[i];
    return static_cast<float>(std::sqrt(sum / numSamples));
}

float EnergyDetector::computeOnsetRate(const float* data, size_t numSamples, int sr)
{
    int windowSize = sr / 20; // 50ms windows
    if (windowSize <= 0 || numSamples < static_cast<size_t>(windowSize * 3))
        return 0.0f;

    int numWindows = static_cast<int>(numSamples) / windowSize;
    std::vector<float> energy(numWindows);

    for (int w = 0; w < numWindows; ++w) {
        float e = 0.0f;
        size_t start = static_cast<size_t>(w) * windowSize;
        for (int s = 0; s < windowSize; ++s)
            e += data[start + s] * data[start + s];
        energy[w] = e / windowSize;
    }

    int onsetCount = 0;
    float mean = 0.0f;
    for (auto e : energy) mean += e;
    mean /= energy.size();
    float threshold = mean * 1.5f;

    for (int w = 1; w < numWindows; ++w) {
        if (energy[w] - energy[w - 1] > threshold)
            onsetCount++;
    }

    double durationSec = static_cast<double>(numSamples) / sr;
    return static_cast<float>(onsetCount / durationSec); // onsets per second
}

float EnergyDetector::computeSpectralCentroid(const float* data, size_t numSamples, int sr)
{
    int fftSize = 2048;
    if (numSamples < static_cast<size_t>(fftSize)) return 0.0f;

    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    float totalCentroid = 0.0f;
    int count = 0;
    int hopSize = fftSize * 4; // sparse sampling

    for (size_t offset = 0; offset + fftSize <= numSamples; offset += hopSize) {
        std::vector<std::complex<float>> spectrum;
        fft.forward(data + offset, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        float sumWeighted = 0.0f, sumMag = 0.0f;
        for (int b = 1; b < static_cast<int>(mag.size()); ++b) {
            float freq = static_cast<float>(b) * sr / fftSize;
            sumWeighted += freq * mag[b];
            sumMag += mag[b];
        }

        if (sumMag > 0.0f)
            totalCentroid += sumWeighted / sumMag;
        count++;
    }

    return count > 0 ? totalCentroid / count : 0.0f;
}

float EnergyDetector::computeBassRatio(const float* data, size_t numSamples, int sr)
{
    int fftSize = 2048;
    if (numSamples < static_cast<size_t>(fftSize)) return 0.0f;

    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    int bassBinLimit = std::min(static_cast<int>(250.0 * fftSize / sr), fftSize / 2);
    float totalBass = 0.0f, totalAll = 0.0f;
    int hopSize = fftSize * 4;

    for (size_t offset = 0; offset + fftSize <= numSamples; offset += hopSize) {
        std::vector<std::complex<float>> spectrum;
        fft.forward(data + offset, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        for (int b = 1; b < static_cast<int>(mag.size()); ++b) {
            float e = mag[b] * mag[b];
            totalAll += e;
            if (b < bassBinLimit) totalBass += e;
        }
    }

    return totalAll > 0.0f ? totalBass / totalAll : 0.0f;
}

float EnergyDetector::computeHighFreqEnergy(const float* data, size_t numSamples, int sr)
{
    int fftSize = 2048;
    if (numSamples < static_cast<size_t>(fftSize)) return 0.0f;

    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    int highBinStart = std::max(1, static_cast<int>(8000.0 * fftSize / sr));
    float totalHigh = 0.0f, totalAll = 0.0f;
    int hopSize = fftSize * 4;

    for (size_t offset = 0; offset + fftSize <= numSamples; offset += hopSize) {
        std::vector<std::complex<float>> spectrum;
        fft.forward(data + offset, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        for (int b = 1; b < static_cast<int>(mag.size()); ++b) {
            float e = mag[b] * mag[b];
            totalAll += e;
            if (b >= highBinStart) totalHigh += e;
        }
    }

    return totalAll > 0.0f ? totalHigh / totalAll : 0.0f;
}

int EnergyDetector::scoreToEnergy(float score)
{
    if (score < 0.05f) return 1;
    if (score < 0.12f) return 2;
    if (score < 0.20f) return 3;
    if (score < 0.30f) return 4;
    if (score < 0.40f) return 5;
    if (score < 0.52f) return 6;
    if (score < 0.65f) return 7;
    if (score < 0.78f) return 8;
    if (score < 0.90f) return 9;
    return 10;
}

EnergyDetector::EnergyResult EnergyDetector::detect(const AudioTrack& track, double bpm)
{
    EnergyResult result;

    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    if (sr <= 0 || numSamples == 0) return result;

    spdlog::info("[EnergyDetector] Analyzing {} samples at {}Hz", numSamples, sr);

    float rms = computeRMS(data, numSamples);
    float onsetRate = computeOnsetRate(data, numSamples, sr);
    float centroid = computeSpectralCentroid(data, numSamples, sr);
    float bassRatio = computeBassRatio(data, numSamples, sr);
    float highFreq = computeHighFreqEnergy(data, numSamples, sr);

    float rmsNorm = std::clamp(rms * 8.0f, 0.0f, 1.0f);                    // typical RMS 0-0.12
    float onsetNorm = std::clamp(onsetRate / 12.0f, 0.0f, 1.0f);           // 0-12 onsets/sec
    float centroidNorm = std::clamp(centroid / 6000.0f, 0.0f, 1.0f);       // 0-6000 Hz
    float bassNorm = std::clamp(bassRatio * 3.0f, 0.0f, 1.0f);             // 0-0.33 ratio
    float highNorm = std::clamp(highFreq * 20.0f, 0.0f, 1.0f);             // hi-hat/noise presence

    float bpmNorm = 0.5f;
    if (bpm > 0) {
        bpmNorm = std::clamp(static_cast<float>((bpm - 80.0) / 90.0), 0.0f, 1.0f); // 80-170 BPM
    }

    float score = rmsNorm * 0.20f
                + onsetNorm * 0.25f     // percussive density is the strongest indicator
                + centroidNorm * 0.10f
                + bassNorm * 0.15f
                + highNorm * 0.10f      // hi-hat/white noise = buildup indicator
                + bpmNorm * 0.20f;      // tempo matters

    result.rawScore = score;
    result.overallEnergy = scoreToEnergy(score);

    spdlog::info("[EnergyDetector] RMS={:.3f} Onset={:.1f}/s Centroid={:.0f}Hz Bass={:.2f} HiFreq={:.3f} BPM={:.0f} -> Energy={}",
                 rms, onsetRate, centroid, bassRatio, highFreq, bpm, result.overallEnergy);

    double duration = static_cast<double>(numSamples) / sr;
    double segmentDur = (bpm > 0) ? (240.0 / bpm * 8.0) : 15.0; // 8 bars or 15 seconds
    int numSegments = std::max(1, static_cast<int>(duration / segmentDur));

    for (int seg = 0; seg < numSegments; ++seg) {
        double segStart = seg * segmentDur;
        double segEnd = std::min(segStart + segmentDur, duration);
        size_t s0 = static_cast<size_t>(segStart * sr);
        size_t s1 = std::min(static_cast<size_t>(segEnd * sr), numSamples);
        if (s1 <= s0) continue;

        size_t segLen = s1 - s0;
        float segRms = computeRMS(data + s0, segLen);
        float segOnset = computeOnsetRate(data + s0, segLen, sr);

        float segRmsNorm = std::clamp(segRms * 8.0f, 0.0f, 1.0f);
        float segOnsetNorm = std::clamp(segOnset / 12.0f, 0.0f, 1.0f);

        float segScore = segRmsNorm * 0.35f + segOnsetNorm * 0.35f + bpmNorm * 0.30f;

        EnergySegment es;
        es.startTime = segStart;
        es.endTime = segEnd;
        es.energy = scoreToEnergy(segScore);
        result.segments.push_back(es);
    }

    return result;
}

} // namespace BeatMate::Core
