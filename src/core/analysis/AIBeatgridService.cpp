#include "AIBeatgridService.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include "OnsetDetector.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

AIBeatgridService::AIBeatgridService() = default;
AIBeatgridService::~AIBeatgridService() = default;

std::vector<double> AIBeatgridService::multiFeatureOnsetDetection(const AudioTrack& track) {
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    int fftSize = 2048;
    int hopSize = 512;
    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    int numFrames = static_cast<int>((numSamples - fftSize) / hopSize) + 1;
    std::vector<double> onsetFunction;
    onsetFunction.reserve(numFrames);

    std::vector<float> prevMag(fftSize / 2 + 1, 0.0f);
    std::vector<float> prevPhase(fftSize / 2 + 1, 0.0f);
    std::vector<float> prevPrevPhase(fftSize / 2 + 1, 0.0f);

    for (int frame = 0; frame < numFrames; ++frame) {
        size_t offset = frame * hopSize;
        if (offset + fftSize > numSamples) break;

        std::vector<std::complex<float>> spectrum;
        fft.forward(data + offset, spectrum);
        auto mag = fft.getMagnitudes(spectrum);
        auto phase = fft.getPhases(spectrum);

        double spectralFlux = 0.0;
        for (size_t bin = 0; bin < mag.size(); ++bin) {
            double diff = mag[bin] - prevMag[bin];
            if (diff > 0) spectralFlux += diff;
        }

        double phaseDeviation = 0.0;
        if (frame >= 2) {
            for (size_t bin = 1; bin < phase.size(); ++bin) {
                float expectedPhase = 2.0f * prevPhase[bin] - prevPrevPhase[bin];
                float deviation = phase[bin] - expectedPhase;
                while (deviation > 3.14159f) deviation -= 2.0f * 3.14159f;
                while (deviation < -3.14159f) deviation += 2.0f * 3.14159f;
                phaseDeviation += std::fabs(deviation) * mag[bin];
            }
        }

        double hfc = 0.0;
        for (size_t bin = 0; bin < mag.size(); ++bin) {
            hfc += bin * mag[bin] * mag[bin];
        }

        double combined = spectralFlux * 0.5 + phaseDeviation * 0.3 + hfc * 0.0001 * 0.2;
        onsetFunction.push_back(combined);

        prevPrevPhase = prevPhase;
        prevPhase = phase;
        prevMag = mag;
    }

    double hopDuration = static_cast<double>(hopSize) / sr;
    std::vector<double> peaks;

    int medianWindow = static_cast<int>(0.5 / hopDuration); // 500ms window
    double minInterval = 0.1; // 100ms minimum between onsets
    int minFrames = static_cast<int>(minInterval / hopDuration);
    int lastPeak = -minFrames;

    for (int i = 1; i < static_cast<int>(onsetFunction.size()) - 1; ++i) {
        int start = std::max(0, i - medianWindow);
        int end = std::min(static_cast<int>(onsetFunction.size()), i + medianWindow);
        std::vector<double> window(onsetFunction.begin() + start, onsetFunction.begin() + end);
        std::sort(window.begin(), window.end());
        double median = window[window.size() / 2];
        double threshold = median * 1.5 + 0.01;

        if (onsetFunction[i] > threshold &&
            onsetFunction[i] > onsetFunction[i - 1] &&
            onsetFunction[i] > onsetFunction[i + 1] &&
            (i - lastPeak) >= minFrames) {
            peaks.push_back(i * hopDuration);
            lastPeak = i;
        }
    }

    return peaks;
}

double AIBeatgridService::multiMethodTempoEstimation(const std::vector<double>& onsets, int sampleRate) {
    if (onsets.size() < 4) return 0.0;

    std::vector<double> intervals;
    for (size_t i = 1; i < onsets.size(); ++i) {
        double interval = onsets[i] - onsets[i - 1];
        if (interval > 0.2 && interval < 2.0) { // Reasonable beat range
            intervals.push_back(interval);
        }
    }

    if (intervals.empty()) return 0.0;

    int numBins = 2000; // 0-2000ms
    std::vector<int> histogram(numBins, 0);
    for (double interval : intervals) {
        int bin = static_cast<int>(interval * 1000);
        if (bin >= 0 && bin < numBins) {
            for (int d = -5; d <= 5; ++d) {
                int b = bin + d;
                if (b >= 0 && b < numBins) {
                    histogram[b] += 10 - std::abs(d) * 2;
                }
            }
        }
    }

    int bestBin = 0, bestCount = 0;
    for (int bin = static_cast<int>(60000.0 / maxBPM_); bin < static_cast<int>(60000.0 / minBPM_) && bin < numBins; ++bin) {
        int count = histogram[bin];
        int halfBin = bin / 2;
        int doubleBin = bin * 2;
        if (halfBin >= 0 && halfBin < numBins) count += histogram[halfBin] / 2;
        if (doubleBin >= 0 && doubleBin < numBins) count += histogram[doubleBin] / 2;

        if (count > bestCount) {
            bestCount = count;
            bestBin = bin;
        }
    }

    double estimatedBPM = 60000.0 / std::max(1, bestBin);

    if (estimatedBPM >= 62 && estimatedBPM <= 68) estimatedBPM *= 2; // Likely half-time
    if (estimatedBPM >= 155 && estimatedBPM <= 200) {} // Could be D&B
    if (estimatedBPM > 200) estimatedBPM /= 2;

    return estimatedBPM;
}

std::vector<double> AIBeatgridService::dpBeatTracking(const std::vector<double>& onsets,
                                                        double initialTempo, int sampleRate) {
    if (onsets.size() < 2) return onsets;

    double beatPeriod = 60.0 / initialTempo;
    double tolerance = 0.2; // 20% tolerance

    std::vector<double> beats;
    beats.push_back(onsets[0]);

    size_t onsetIdx = 1;
    double expectedNext = onsets[0] + beatPeriod;

    while (expectedNext < onsets.back() + beatPeriod) {
        double bestOnset = expectedNext;
        double bestDist = beatPeriod * tolerance;
        bool found = false;

        while (onsetIdx < onsets.size() && onsets[onsetIdx] < expectedNext + beatPeriod * tolerance) {
            double dist = std::fabs(onsets[onsetIdx] - expectedNext);
            if (dist < bestDist) {
                bestDist = dist;
                bestOnset = onsets[onsetIdx];
                found = true;
            }
            if (onsets[onsetIdx] > expectedNext + beatPeriod * tolerance) break;
            onsetIdx++;
        }

        if (!found) {
            bestOnset = expectedNext;
        }

        beats.push_back(bestOnset);
        expectedNext = bestOnset + beatPeriod;
    }

    return beats;
}

BeatGrid AIBeatgridService::refineGrid(const std::vector<double>& beats,
                                         const std::vector<double>& onsets, double bpm) {
    BeatGrid grid;
    grid.bpm = bpm;
    grid.beatPositions = beats;
    grid.beatsPerBar = 4;

    if (!beats.empty()) {
        grid.firstBeatOffset = beats[0];
    }

    for (size_t i = 0; i < beats.size(); i += grid.beatsPerBar) {
        grid.barPositions.push_back(beats[i]);
    }

    return grid;
}

AIBeatgridResult AIBeatgridService::generate(const AudioTrack& track) {
    auto startTime = std::chrono::steady_clock::now();

    spdlog::info("AIBeatgridService: generating AI beatgrid for {}", track.getFilePath());

    AIBeatgridResult result;
    int sr = track.getSampleRate();

    auto onsets = multiFeatureOnsetDetection(track);
    spdlog::debug("AIBeatgridService: {} onsets detected", onsets.size());

    if (onsets.size() < 4) {
        spdlog::warn("AIBeatgridService: too few onsets");
        return result;
    }

    double tempo = multiMethodTempoEstimation(onsets, sr);
    spdlog::debug("AIBeatgridService: estimated tempo {:.2f} BPM", tempo);
    if (tempo <= 0.0)
        return result;

    auto beats = dpBeatTracking(onsets, tempo, sr);
    spdlog::debug("AIBeatgridService: {} beats tracked", beats.size());

    if (beats.size() > 4) {
        std::vector<double> intervals;
        for (size_t i = 1; i < beats.size(); ++i) {
            double interval = beats[i] - beats[i - 1];
            if (interval > 0.2 && interval < 2.0) {
                intervals.push_back(60.0 / interval);
            }
        }
        if (!intervals.empty()) {
            std::sort(intervals.begin(), intervals.end());
            double median = intervals[intervals.size() / 2];
            std::vector<double> filtered;
            for (auto& v : intervals) {
                if (std::fabs(v - median) / median < 0.02) filtered.push_back(v);
            }
            if (!filtered.empty()) {
                double sum = std::accumulate(filtered.begin(), filtered.end(), 0.0);
                tempo = sum / filtered.size();
            }
        }
    }

    tempo = std::round(tempo * 10.0) / 10.0;

    result.grid = refineGrid(beats, onsets, tempo);
    result.method = "hybrid";

    if (beats.size() > 8) {
        double segmentSize = beats.size() / 4;
        std::vector<double> segmentBPMs;

        for (int seg = 0; seg < 4; ++seg) {
            size_t start = static_cast<size_t>(seg * segmentSize);
            size_t end = std::min(static_cast<size_t>((seg + 1) * segmentSize), beats.size() - 1);

            double segSum = 0.0;
            int segCount = 0;
            for (size_t i = start + 1; i <= end; ++i) {
                double interval = beats[i] - beats[i - 1];
                if (interval > 0.2 && interval < 2.0) {
                    segSum += 60.0 / interval;
                    segCount++;
                }
            }
            if (segCount > 0) segmentBPMs.push_back(segSum / segCount);
        }

        if (segmentBPMs.size() >= 2) {
            double minBPM = *std::min_element(segmentBPMs.begin(), segmentBPMs.end());
            double maxBPMVal = *std::max_element(segmentBPMs.begin(), segmentBPMs.end());
            double variation = (maxBPMVal - minBPM) / tempo * 100.0;

            result.isVariableTempo = variation > 2.0;

            if (result.isVariableTempo) {
                for (size_t i = 0; i < segmentBPMs.size(); ++i) {
                    result.tempoChanges.push_back(beats[static_cast<size_t>(i * segmentSize)]);
                    result.tempoValues.push_back(segmentBPMs[i]);
                }
            }
        }
    }

    float alignmentScore = 0.0f;
    for (auto& beat : beats) {
        double minDist = 1e10;
        for (auto& onset : onsets) {
            minDist = std::min(minDist, std::fabs(onset - beat));
        }
        if (minDist < 0.02) alignmentScore += 1.0f; // Within 20ms
    }
    result.confidence = beats.empty() ? 0.0f : alignmentScore / beats.size();

    auto endTime = std::chrono::steady_clock::now();
    result.processingTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    spdlog::info("AIBeatgridService: {:.2f} BPM, {} beats, confidence={:.0f}%, variable={}, {:.0f}ms",
                 result.grid.bpm, result.grid.beatPositions.size(),
                 result.confidence * 100, result.isVariableTempo ? "yes" : "no",
                 result.processingTimeMs);
    return result;
}

} // namespace BeatMate::Core
