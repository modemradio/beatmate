#include "RealEnergyAnalysisService.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

RealEnergyAnalysisService::RealEnergyAnalysisService() = default;
RealEnergyAnalysisService::~RealEnergyAnalysisService() = default;

float RealEnergyAnalysisService::computeRMS(const float* data, size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    double sum = 0.0;
    for (size_t i = 0; i < numSamples; ++i) sum += data[i] * data[i];
    return static_cast<float>(std::sqrt(sum / numSamples));
}

float RealEnergyAnalysisService::computeSpectralCentroid(const std::vector<float>& mag,
                                                           int sampleRate, int fftSize) {
    double weightedSum = 0.0, magSum = 0.0;
    for (int i = 1; i < static_cast<int>(mag.size()); ++i) {
        double freq = static_cast<double>(i) * sampleRate / fftSize;
        weightedSum += freq * mag[i];
        magSum += mag[i];
    }
    return (magSum > 0) ? static_cast<float>(weightedSum / magSum) : 0.0f;
}

float RealEnergyAnalysisService::computeSpectralSpread(const std::vector<float>& mag,
                                                         float centroid, int sampleRate, int fftSize) {
    double weightedSum = 0.0, magSum = 0.0;
    for (int i = 1; i < static_cast<int>(mag.size()); ++i) {
        double freq = static_cast<double>(i) * sampleRate / fftSize;
        double diff = freq - centroid;
        weightedSum += diff * diff * mag[i];
        magSum += mag[i];
    }
    return (magSum > 0) ? static_cast<float>(std::sqrt(weightedSum / magSum)) : 0.0f;
}

float RealEnergyAnalysisService::computeSpectralFlatness(const std::vector<float>& mag) {
    if (mag.size() < 2) return 0.0f;

    double logSum = 0.0;
    double linSum = 0.0;
    int count = 0;

    for (size_t i = 1; i < mag.size(); ++i) {
        if (mag[i] > 1e-10f) {
            logSum += std::log(mag[i]);
            linSum += mag[i];
            count++;
        }
    }

    if (count == 0 || linSum <= 0) return 0.0f;

    double geometricMean = std::exp(logSum / count);
    double arithmeticMean = linSum / count;
    return static_cast<float>(std::clamp(geometricMean / arithmeticMean, 0.0, 1.0));
}

float RealEnergyAnalysisService::computeSpectralRolloff(const std::vector<float>& mag,
                                                          int sampleRate, int fftSize, float percent) {
    double totalEnergy = 0.0;
    for (size_t i = 1; i < mag.size(); ++i) totalEnergy += mag[i] * mag[i];

    double threshold = totalEnergy * percent;
    double cumEnergy = 0.0;

    for (size_t i = 1; i < mag.size(); ++i) {
        cumEnergy += mag[i] * mag[i];
        if (cumEnergy >= threshold) {
            return static_cast<float>(static_cast<double>(i) * sampleRate / fftSize);
        }
    }
    return static_cast<float>(sampleRate / 2);
}

EnergyBand RealEnergyAnalysisService::computeBandEnergy(const std::vector<float>& mag,
                                                          int sampleRate, int fftSize,
                                                          float freqLow, float freqHigh) {
    EnergyBand band;
    int binLow = std::max(1, static_cast<int>(freqLow * fftSize / sampleRate));
    int binHigh = std::min(static_cast<int>(mag.size()) - 1, static_cast<int>(freqHigh * fftSize / sampleRate));

    if (binLow >= binHigh) return band;

    float sum = 0.0f, peak = 0.0f;
    int count = 0;
    for (int i = binLow; i <= binHigh; ++i) {
        sum += mag[i] * mag[i];
        peak = std::max(peak, mag[i]);
        count++;
    }

    band.rms = (count > 0) ? std::sqrt(sum / count) : 0.0f;
    band.peak = peak;
    band.energy = band.rms;
    band.crest = (band.rms > 1e-10f) ? 20.0f * std::log10(band.peak / band.rms) : 0.0f;

    return band;
}

RealEnergyResult RealEnergyAnalysisService::analyze(const AudioTrack& track, double segmentDuration) {
    spdlog::info("RealEnergyAnalysisService: analyzing {}", track.getFilePath());

    RealEnergyResult result;
    result.segmentDuration = segmentDuration;

    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t totalSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    if (totalSamples == 0) return result;

    int fftSize = 4096;
    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    size_t segSamples = static_cast<size_t>(segmentDuration * sr);
    size_t numSegments = totalSamples / segSamples;
    if (numSegments == 0) numSegments = 1;

    std::vector<float> globalMag(fftSize / 2 + 1, 0.0f);
    int globalFrames = 0;

    float globalRMSSum = 0.0f;
    float peakLevel = 0.0f;
    float minRMS = 1e10f, maxRMS = 0.0f;

    for (size_t seg = 0; seg < numSegments; ++seg) {
        const float* segData = data + seg * segSamples;
        size_t segLen = std::min(segSamples, totalSamples - seg * segSamples);

        float segRMS = computeRMS(segData, segLen);
        result.energyCurve.push_back(segRMS);
        globalRMSSum += segRMS;
        minRMS = std::min(minRMS, segRMS);
        maxRMS = std::max(maxRMS, segRMS);

        for (size_t i = 0; i < segLen; ++i) {
            peakLevel = std::max(peakLevel, std::fabs(segData[i]));
        }

        if (segLen >= static_cast<size_t>(fftSize)) {
            size_t center = segLen / 2 - fftSize / 2;
            std::vector<std::complex<float>> spectrum;
            fft.forward(segData + center, spectrum);
            auto mag = fft.getMagnitudes(spectrum);

            for (size_t i = 0; i < mag.size(); ++i) {
                globalMag[i] += mag[i];
            }
            globalFrames++;

            float centroid = computeSpectralCentroid(mag, sr, fftSize);
            result.centroidCurve.push_back(centroid);

            float segLUFS = -0.691f + 10.0f * std::log10(std::max(segRMS * segRMS, 1e-10f));
            result.loudnessCurve.push_back(segLUFS);
        }
    }

    if (globalFrames > 0) {
        for (auto& m : globalMag) m /= globalFrames;
    }

    result.rmsGlobal = globalRMSSum / numSegments;
    result.spectralCentroid = computeSpectralCentroid(globalMag, sr, fftSize);
    result.spectralSpread = computeSpectralSpread(globalMag, result.spectralCentroid, sr, fftSize);
    result.spectralFlatness = computeSpectralFlatness(globalMag);
    result.spectralRolloff = computeSpectralRolloff(globalMag, sr, fftSize);

    float rmsDb = 20.0f * std::log10(std::max(result.rmsGlobal, 1e-10f));
    float peakDb = 20.0f * std::log10(std::max(peakLevel, 1e-10f));
    float minRMSDb = 20.0f * std::log10(std::max(minRMS, 1e-10f));
    float maxRMSDb = 20.0f * std::log10(std::max(maxRMS, 1e-10f));
    result.dynamicRange = maxRMSDb - minRMSDb;

    result.lufs = -0.691f + 10.0f * std::log10(std::max(result.rmsGlobal * result.rmsGlobal, 1e-10f));

    result.subBass = computeBandEnergy(globalMag, sr, fftSize, 20.0f, 60.0f);
    result.bass = computeBandEnergy(globalMag, sr, fftSize, 60.0f, 250.0f);
    result.lowMid = computeBandEnergy(globalMag, sr, fftSize, 250.0f, 500.0f);
    result.mid = computeBandEnergy(globalMag, sr, fftSize, 500.0f, 2000.0f);
    result.highMid = computeBandEnergy(globalMag, sr, fftSize, 2000.0f, 4000.0f);
    result.presence = computeBandEnergy(globalMag, sr, fftSize, 4000.0f, 6000.0f);
    result.brilliance = computeBandEnergy(globalMag, sr, fftSize, 6000.0f, 20000.0f);

    float maxEnergy = 0.0f;
    for (auto& e : result.energyCurve) maxEnergy = std::max(maxEnergy, e);
    if (maxEnergy > 0) {
        for (auto& e : result.energyCurve) e /= maxEnergy;
    }

    float normalized = (rmsDb + 40.0f) / 34.0f;
    result.overallEnergy = static_cast<int>(std::clamp(normalized * 10.0f, 1.0f, 10.0f));

    spdlog::info("RealEnergyAnalysisService: energy={}/10, centroid={:.0f}Hz, lufs={:.1f}, DR={:.1f}dB",
                 result.overallEnergy, result.spectralCentroid, result.lufs, result.dynamicRange);
    return result;
}

} // namespace BeatMate::Core
