#include "BPMDetector.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

BPMDetector::BPMDetector() = default;
BPMDetector::~BPMDetector() = default;

std::vector<double> BPMDetector::computeSpectralFlux(const float* mono, size_t numSamples,
                                                      int sampleRate, int fftSize, int hopSize) {
    if (!mono || numSamples < static_cast<size_t>(fftSize)) {
        spdlog::warn("BPMDetector: insufficient samples ({}) for FFT size ({})", numSamples, fftSize);
        return {};
    }

    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    int numFrames = static_cast<int>((numSamples - fftSize) / hopSize) + 1;
    std::vector<double> flux;
    flux.reserve(numFrames);

    std::vector<float> prevMag(fftSize / 2 + 1, 0.0f);

    for (int frame = 0; frame < numFrames; ++frame) {
        size_t offset = static_cast<size_t>(frame) * static_cast<size_t>(hopSize);
        if (offset + fftSize > numSamples) break;

        std::vector<std::complex<float>> spectrum;
        fft.forward(mono + offset, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        double sf = 0.0;
        for (size_t bin = 0; bin < mag.size(); ++bin) {
            double diff = mag[bin] - prevMag[bin];
            if (diff > 0) sf += diff;
        }
        flux.push_back(sf);
        prevMag = mag;
    }

    return flux;
}

std::vector<double> BPMDetector::computeOnsetFunction(const AudioTrack& track) {
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    if (!data || numSamples == 0 || sr == 0) {
        spdlog::warn("BPMDetector: invalid mono track (samples={}, sr={})", numSamples, sr);
        return {};
    }

    int fftSize = 2048;
    int hopSize = 512;

    spdlog::debug("BPMDetector: onset function on {} samples, {}Hz", numSamples, sr);
    return computeSpectralFlux(data, numSamples, sr, fftSize, hopSize);
}

double BPMDetector::estimateTempo(const std::vector<double>& onsetFunction, int sampleRate) {
    int hopSize = 512;
    double hopDuration = static_cast<double>(hopSize) / sampleRate;

    int minLag = static_cast<int>(60.0 / maxBPM_ / hopDuration);
    int maxLag = static_cast<int>(60.0 / minBPM_ / hopDuration);
    maxLag = std::min(maxLag, static_cast<int>(onsetFunction.size()) / 2);

    if (minLag < 1 || maxLag <= minLag) {
        spdlog::warn("BPMDetector::estimateTempo: onset function too short (minLag={}, maxLag={}), BPM undetermined", minLag, maxLag);
        return 0.0;
    }

    std::vector<double> autocorr(maxLag + 1, 0.0);
    int len = static_cast<int>(onsetFunction.size());

    double mean = std::accumulate(onsetFunction.begin(), onsetFunction.end(), 0.0) / len;

    for (int lag = minLag; lag <= maxLag; ++lag) {
        double sum = 0.0;
        int count = 0;
        for (int i = 0; i + lag < len; ++i) {
            sum += (onsetFunction[i] - mean) * (onsetFunction[i + lag] - mean);
            count++;
        }
        if (count > 0) autocorr[lag] = sum / count;
    }

    double bestBPM = 0.0;
    double bestVal = -1e10;

    for (int lag = minLag; lag <= maxLag; ++lag) {
        double bpm = 60.0 / (lag * hopDuration);
        double val = autocorr[lag];

        // Boost common DJ BPM ranges (aggressive)
        if (bpm >= 118 && bpm <= 135) val *= 1.5;  // House/Techno
        if (bpm >= 140 && bpm <= 160) val *= 1.3;  // D&B/Dubstep
        if (bpm >= 90 && bpm <= 110) val *= 1.2;   // Hip-hop

        int doubleLag = lag / 2;
        if (doubleLag >= minLag && doubleLag < static_cast<int>(autocorr.size())) {
            val += autocorr[doubleLag] * 0.5;
        }

        if (val > bestVal) {
            bestVal = val;
            bestBPM = bpm;
        }
    }

    if (bestBPM <= 0.0)
        return 0.0;

    while (bestBPM < 80.0 && bestBPM > 0.0) bestBPM *= 2.0;
    while (bestBPM > 180.0) bestBPM /= 2.0;

    // Comb filter refinement: scan +/- 5 BPM around coarse estimate
    {
        double coarseBPM = bestBPM;
        double bestScore = -1e10;
        for (double bpm = coarseBPM - 5.0; bpm <= coarseBPM + 5.0; bpm += 0.1) {
            double beatPeriod = 60.0 / bpm / hopDuration;
            double score = 0.0;
            for (int beat = 0; beat < 200; ++beat) {
                int idx = static_cast<int>(beat * beatPeriod);
                if (idx >= 0 && idx < static_cast<int>(onsetFunction.size()))
                    score += onsetFunction[idx];
            }
            if (score > bestScore) {
                bestScore = score;
                bestBPM = bpm;
            }
        }
    }

    while (bestBPM < 80.0 && bestBPM > 0.0) bestBPM *= 2.0;
    while (bestBPM > 180.0) bestBPM /= 2.0;

    bestBPM = std::round(bestBPM * 10.0) / 10.0;

    return bestBPM;
}

std::vector<double> BPMDetector::trackBeats(const std::vector<double>& onsetFunction,
                                             double tempo, int hopSize, int sampleRate) {
    double hopDuration = static_cast<double>(hopSize) / sampleRate;
    double beatPeriodFrames = 60.0 / tempo / hopDuration;

    std::vector<double> beats;
    int len = static_cast<int>(onsetFunction.size());

    double threshold = 0.0;
    for (auto& v : onsetFunction) threshold += v;
    threshold = threshold / len * 0.5;

    double expectedPos = 0;
    while (expectedPos < len) {
        int searchStart = std::max(0, static_cast<int>(expectedPos - beatPeriodFrames * 0.2));
        int searchEnd = std::min(len - 1, static_cast<int>(expectedPos + beatPeriodFrames * 0.2));

        double bestVal = -1;
        int bestPos = static_cast<int>(expectedPos);

        for (int j = searchStart; j <= searchEnd; ++j) {
            if (onsetFunction[j] > bestVal && onsetFunction[j] > threshold) {
                bestVal = onsetFunction[j];
                bestPos = j;
            }
        }

        double beatTimeSec = bestPos * hopDuration;
        beats.push_back(beatTimeSec);

        expectedPos = bestPos + beatPeriodFrames;
    }

    return beats;
}

BPMResult BPMDetector::detect(const AudioTrack& track) {
    spdlog::info("BPMDetector: analyzing {} ({:.1f}s)", track.getFilePath(), track.getDuration());

    BPMResult result;
    auto onset = computeOnsetFunction(track);
    if (onset.empty()) {
        spdlog::warn("BPMDetector: empty onset function");
        return result;
    }

    int sr = track.getSampleRate();
    result.bpm = estimateTempo(onset, sr);
    result.beats = trackBeats(onset, result.bpm, 512, sr);

    double peakStrength = 0.0;
    double mean = 0.0;
    for (auto& v : onset) mean += v;
    mean /= onset.size();
    for (auto& v : onset) peakStrength += std::fabs(v - mean);
    peakStrength /= onset.size();

    result.confidence = std::clamp(peakStrength * 5.0, 0.0, 1.0);

    if (!result.beats.empty()) {
        result.offset = result.beats[0];
    }

    spdlog::info("BPMDetector: detected {:.1f} BPM (confidence: {:.0f}%)",
                 result.bpm, result.confidence * 100);
    return result;
}

} // namespace BeatMate::Core
