#include "AdvancedBeatRefinementService.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include "OnsetDetector.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

AdvancedBeatRefinementService::AdvancedBeatRefinementService() = default;
AdvancedBeatRefinementService::~AdvancedBeatRefinementService() = default;

std::vector<float> AdvancedBeatRefinementService::computeOnsetStrengths(
    const AudioTrack& track, const std::vector<double>& beatPositions) {

    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    int fftSize = 1024;
    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    std::vector<float> strengths;
    strengths.reserve(beatPositions.size());

    for (double pos : beatPositions) {
        size_t samplePos = static_cast<size_t>(pos * sr);
        if (samplePos + fftSize >= numSamples) {
            strengths.push_back(0.0f);
            continue;
        }

        std::vector<std::complex<float>> spectrum;
        fft.forward(data + samplePos, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        float energy = 0.0f;
        for (auto& m : mag) energy += m * m;
        strengths.push_back(std::sqrt(energy / mag.size()));
    }

    return strengths;
}

std::vector<double> AdvancedBeatRefinementService::snapToOnsets(
    const std::vector<double>& beats, const std::vector<double>& onsets, double maxShiftMs) {

    double maxShiftSec = maxShiftMs / 1000.0;
    std::vector<double> snapped;
    snapped.reserve(beats.size());

    for (double beat : beats) {
        double bestOnset = beat;
        double bestDist = maxShiftSec;

        for (double onset : onsets) {
            double dist = std::fabs(onset - beat);
            if (dist < bestDist) {
                bestDist = dist;
                bestOnset = onset;
            }
        }
        snapped.push_back(bestOnset);
    }

    return snapped;
}

std::vector<double> AdvancedBeatRefinementService::detectDrift(
    const std::vector<double>& beats, double expectedBPM) {

    double expectedInterval = 60.0 / expectedBPM;
    std::vector<double> drifts;
    drifts.reserve(beats.size());

    for (size_t i = 0; i < beats.size(); ++i) {
        double expectedPos = beats[0] + i * expectedInterval;
        double drift = (beats[i] - expectedPos) * 1000.0;
        drifts.push_back(drift);
    }

    return drifts;
}

BeatGrid AdvancedBeatRefinementService::iterativeAlign(
    const AudioTrack& track, const BeatGrid& grid, int iterations) {

    BeatGrid aligned = grid;
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    for (int iter = 0; iter < iterations; ++iter) {
        auto strengths = computeOnsetStrengths(track, aligned.beatPositions);

        double searchSec = maxCorrectionMs_ / 1000.0;
        int searchSamples = static_cast<int>(searchSec * sr);

        for (size_t i = 0; i < aligned.beatPositions.size(); ++i) {
            size_t centerSample = static_cast<size_t>(aligned.beatPositions[i] * sr);
            size_t startSample = (centerSample > static_cast<size_t>(searchSamples))
                                 ? centerSample - searchSamples : 0;
            size_t endSample = std::min(centerSample + searchSamples, numSamples - 1024);

            float bestStrength = 0.0f;
            size_t bestPos = centerSample;

            int step = std::max(1, searchSamples / 50);
            for (size_t s = startSample; s < endSample; s += step) {
                float energy = 0.0f;
                int windowSize = std::min(512, static_cast<int>(numSamples - s));
                for (int j = 0; j < windowSize; ++j) {
                    energy += data[s + j] * data[s + j];
                }
                energy = std::sqrt(energy / windowSize);

                double distFactor = 1.0 - std::fabs(static_cast<double>(s) - centerSample) /
                                    searchSamples * (1.0 - onsetWeight_);

                float score = energy * static_cast<float>(distFactor);
                if (score > bestStrength) {
                    bestStrength = score;
                    bestPos = s;
                }
            }

            aligned.beatPositions[i] = static_cast<double>(bestPos) / sr;
        }
    }

    aligned.barPositions.clear();
    for (size_t i = 0; i < aligned.beatPositions.size(); i += aligned.beatsPerBar) {
        aligned.barPositions.push_back(aligned.beatPositions[i]);
    }

    return aligned;
}

RefinedBeatGrid AdvancedBeatRefinementService::refine(const AudioTrack& track, const BeatGrid& rawGrid) {
    spdlog::info("AdvancedBeatRefinementService: refining grid ({} beats, {:.2f} BPM)",
                 rawGrid.beatPositions.size(), rawGrid.bpm);

    RefinedBeatGrid result;

    if (rawGrid.beatPositions.empty()) {
        spdlog::warn("AdvancedBeatRefinementService: empty grid");
        return result;
    }

    OnsetDetector onsetDetector;
    onsetDetector.setThreshold(0.3f);
    auto onsets = onsetDetector.detect(track);

    auto snappedBeats = snapToOnsets(rawGrid.beatPositions, onsets, maxCorrectionMs_);

    BeatGrid snappedGrid = rawGrid;
    snappedGrid.beatPositions = snappedBeats;
    result.grid = iterativeAlign(track, snappedGrid, 3);

    result.corrections.resize(rawGrid.beatPositions.size());
    double totalDev = 0.0;
    result.maxDeviation = 0.0;

    for (size_t i = 0; i < rawGrid.beatPositions.size(); ++i) {
        double correctionMs = (result.grid.beatPositions[i] - rawGrid.beatPositions[i]) * 1000.0;
        result.corrections[i] = correctionMs;
        double absDev = std::fabs(correctionMs);
        totalDev += absDev;
        result.maxDeviation = std::max(result.maxDeviation, absDev);
    }

    result.averageDeviation = totalDev / rawGrid.beatPositions.size();

    result.isConsistent = result.maxDeviation < 15.0;

    auto strengths = computeOnsetStrengths(track, result.grid.beatPositions);
    float avgStrength = 0.0f;
    for (auto& s : strengths) avgStrength += s;
    if (!strengths.empty()) avgStrength /= strengths.size();

    result.gridConfidence = std::clamp(avgStrength * 5.0f, 0.0f, 1.0f);

    spdlog::info("AdvancedBeatRefinementService: avg deviation {:.1f}ms, max {:.1f}ms, confidence {:.0f}%",
                 result.averageDeviation, result.maxDeviation, result.gridConfidence * 100);
    return result;
}

RefinedBeatGrid AdvancedBeatRefinementService::correctDrift(const AudioTrack& track, const BeatGrid& grid) {
    spdlog::info("AdvancedBeatRefinementService: correcting drift");

    auto drifts = detectDrift(grid.beatPositions, grid.bpm);

    if (drifts.size() > 2) {
        double firstDrift = drifts.front();
        double lastDrift = drifts.back();
        double driftPerBeat = (lastDrift - firstDrift) / drifts.size();

        if (std::fabs(driftPerBeat) > 0.1) {
            spdlog::info("AdvancedBeatRefinementService: drift rate {:.3f} ms/beat", driftPerBeat);

            BeatGrid corrected = grid;
            for (size_t i = 0; i < corrected.beatPositions.size(); ++i) {
                double correction = i * driftPerBeat / 1000.0;
                corrected.beatPositions[i] -= correction;
            }

            return refine(track, corrected);
        }
    }

    return refine(track, grid);
}

} // namespace BeatMate::Core
