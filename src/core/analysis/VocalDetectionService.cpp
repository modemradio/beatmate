#include "VocalDetectionService.h"
#include "AudioAnalysisPipeline.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

VocalDetectionService::VocalDetectionService() = default;
VocalDetectionService::~VocalDetectionService() = default;

float VocalDetectionService::computeHNR(const float* data, int numSamples, int sampleRate) {
    // Autocorrelation-based HNR using FFT (Wiener-Khinchin):
    if (numSamples < 512) return 0.0f;

    int maxLag = sampleRate / 75;   // ~75Hz minimum (bass voice)
    int minLag = sampleRate / 500;  // ~500Hz maximum
    maxLag = std::min(maxLag, numSamples / 2);
    if (minLag >= maxLag) return 0.0f;

    // FFT >= 2*numSamples pour lineariser la correlation circulaire
    int fftSize = 1;
    while (fftSize < 2 * numSamples) fftSize <<= 1;

    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Rectangular); // No window: we want raw autocorr.

    std::vector<float> buffer(static_cast<size_t>(fftSize), 0.0f);
    std::copy(data, data + numSamples, buffer.begin());

    std::vector<std::complex<float>> spectrum;
    fft.forward(buffer.data(), spectrum);

    // Spectre de puissance : l'IFFT donne l'autocorrelation (Wiener-Khinchin)
    for (auto& c : spectrum) {
        float p = c.real() * c.real() + c.imag() * c.imag();
        c = std::complex<float>(p, 0.0f);
    }

    std::vector<float> autocorr(static_cast<size_t>(fftSize), 0.0f);
    fft.inverse(spectrum, autocorr.data());

    // r0 doit etre > 0 ; l'IFFT JUCE peut normaliser ou non, on travaille en ratio
    double r0 = autocorr[0];
    if (r0 < 1e-10) return 0.0f;

    double maxR = 0.0;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        double r = autocorr[lag];
        // Compensate for the linear/circular bias (length numSamples - lag).
        int span = numSamples - lag;
        if (span <= 0) continue;
        double normalised = r * static_cast<double>(numSamples) / span;
        if (normalised > maxR) maxR = normalised;
    }

    if (r0 <= maxR || maxR <= 0) return 0.0f;
    double hnr = 10.0 * std::log10(maxR / (r0 - maxR));
    return static_cast<float>(std::clamp(hnr, 0.0, 40.0));
}

float VocalDetectionService::computeVocalSpectralFlatness(const std::vector<float>& mag,
                                                            int sampleRate, int fftSize) {
    int binLow = static_cast<int>(300.0f * fftSize / sampleRate);
    int binHigh = static_cast<int>(4000.0f * fftSize / sampleRate);
    binLow = std::max(1, binLow);
    binHigh = std::min(static_cast<int>(mag.size()) - 1, binHigh);

    double logSum = 0.0, linSum = 0.0;
    int count = 0;

    for (int i = binLow; i <= binHigh; ++i) {
        if (mag[i] > 1e-10f) {
            logSum += std::log(mag[i]);
            linSum += mag[i];
            count++;
        }
    }

    if (count == 0 || linSum <= 0) return 1.0f;

    double geometricMean = std::exp(logSum / count);
    double arithmeticMean = linSum / count;
    return static_cast<float>(geometricMean / arithmeticMean);
}

std::vector<float> VocalDetectionService::computeVocalLikelihood(const AudioTrack& track) {
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    int fftSize = 2048;
    int hopSize = 1024;
    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    // Guard against size_t underflow when the track is shorter than fftSize.
    int numFrames = static_cast<int>(safeFrameCount(numSamples,
                                                    static_cast<size_t>(fftSize),
                                                    static_cast<size_t>(hopSize)));
    std::vector<float> likelihood;
    likelihood.reserve(numFrames);

    for (int frame = 0; frame < numFrames; ++frame) {
        size_t offset = frame * hopSize;
        if (offset + fftSize > numSamples) break;

        std::vector<std::complex<float>> spectrum;
        fft.forward(data + offset, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        float vocalEnergy = 0.0f, totalEnergy = 0.0f;
        int binLow = static_cast<int>(300.0f * fftSize / sr);
        int binHigh = static_cast<int>(4000.0f * fftSize / sr);

        for (int bin = 1; bin < static_cast<int>(mag.size()); ++bin) {
            float e = mag[bin] * mag[bin];
            totalEnergy += e;
            if (bin >= binLow && bin <= binHigh) vocalEnergy += e;
        }
        float vocalRatio = (totalEnergy > 0) ? vocalEnergy / totalEnergy : 0.0f;

        // Feature 2: Spectral flatness (vocals are less flat than noise)
        float flatness = computeVocalSpectralFlatness(mag, sr, fftSize);
        float harmScore = 1.0f - flatness; // More harmonic = more likely vocal

        int windowSamples = std::min(fftSize, static_cast<int>(numSamples - offset));
        float hnr = computeHNR(data + offset, windowSamples, sr);
        float hnrScore = std::clamp(hnr / 20.0f, 0.0f, 1.0f);

        // Feature 4: Spectral centroid in vocal range
        float centroid = 0.0f, magSum = 0.0f;
        for (int bin = binLow; bin <= binHigh && bin < static_cast<int>(mag.size()); ++bin) {
            float freq = static_cast<float>(bin) * sr / fftSize;
            centroid += freq * mag[bin];
            magSum += mag[bin];
        }
        centroid = (magSum > 0) ? centroid / magSum : 0.0f;
        // Vocals typically center around 500-2000 Hz
        float centroidScore = 0.0f;
        if (centroid > 300 && centroid < 3000) {
            centroidScore = 1.0f - std::fabs(centroid - 1200.0f) / 1500.0f;
            centroidScore = std::clamp(centroidScore, 0.0f, 1.0f);
        }

        float score = vocalRatio * 0.25f + harmScore * 0.3f + hnrScore * 0.3f + centroidScore * 0.15f;
        likelihood.push_back(std::clamp(score, 0.0f, 1.0f));
    }

    return likelihood;
}

std::vector<VocalRegion> VocalDetectionService::mergeRegions(
    const std::vector<VocalRegion>& regions, double gap) {

    if (regions.size() < 2) return regions;

    std::vector<VocalRegion> merged;
    VocalRegion current = regions[0];

    for (size_t i = 1; i < regions.size(); ++i) {
        if (regions[i].startTime - current.endTime < gap) {
            current.endTime = regions[i].endTime;
            current.confidence = std::max(current.confidence, regions[i].confidence);
            current.averageEnergy = (current.averageEnergy + regions[i].averageEnergy) / 2.0f;
        } else {
            merged.push_back(current);
            current = regions[i];
        }
    }
    merged.push_back(current);

    return merged;
}

VocalDetectionResult VocalDetectionService::detect(const AudioTrack& track) {
    spdlog::info("VocalDetectionService: analyzing {}", track.getFilePath());

    VocalDetectionResult result;

    auto likelihood = computeVocalLikelihood(track);
    result.vocalCurve = likelihood;

    int hopSize = 1024;
    int sr = track.getSampleRate();
    result.frameDuration = static_cast<double>(hopSize) / sr;

    if (likelihood.empty()) return result;

    std::vector<VocalRegion> rawRegions;
    bool inRegion = false;
    VocalRegion current;

    for (size_t i = 0; i < likelihood.size(); ++i) {
        double time = i * result.frameDuration;

        if (likelihood[i] >= threshold_ && !inRegion) {
            current = {};
            current.startTime = time;
            current.type = "vocal";
            inRegion = true;
        } else if ((likelihood[i] < threshold_ || i == likelihood.size() - 1) && inRegion) {
            current.endTime = time;
            inRegion = false;

            if (current.endTime - current.startTime >= minRegionDuration_) {
                float confSum = 0.0f;
                int count = 0;
                int startFrame = static_cast<int>(current.startTime / result.frameDuration);
                int endFrame = static_cast<int>(current.endTime / result.frameDuration);
                for (int f = startFrame; f <= endFrame && f < static_cast<int>(likelihood.size()); ++f) {
                    confSum += likelihood[f];
                    count++;
                }
                current.confidence = (count > 0) ? confSum / count : 0.0f;
                current.averageEnergy = current.confidence;
                rawRegions.push_back(current);
            }
        }
    }

    result.regions = mergeRegions(rawRegions, 0.5);
    result.hasVocals = !result.regions.empty();

    double totalVocalTime = 0.0;
    for (auto& r : result.regions) totalVocalTime += r.endTime - r.startTime;
    double trackDuration = track.getDuration();
    result.vocalPercentage = (trackDuration > 0.0)
        ? static_cast<float>(totalVocalTime / trackDuration * 100.0)
        : 0.0f;

    if (!result.regions.empty()) {
        float confSum = 0.0f;
        for (auto& r : result.regions) confSum += r.confidence;
        result.overallConfidence = confSum / result.regions.size();
    }

    spdlog::info("VocalDetectionService: {} regions, {:.0f}% vocals, confidence {:.0f}%",
                 result.regions.size(), result.vocalPercentage, result.overallConfidence * 100);
    return result;
}

} // namespace BeatMate::Core
