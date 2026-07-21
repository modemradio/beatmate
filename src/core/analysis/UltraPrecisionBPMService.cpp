#include "UltraPrecisionBPMService.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

UltraPrecisionBPMService::UltraPrecisionBPMService() = default;
UltraPrecisionBPMService::~UltraPrecisionBPMService() = default;

double UltraPrecisionBPMService::coarsePass(const float* mono, size_t numSamples, int sampleRate) {
    int fftSize = 2048;
    int hopSize = 512;
    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    int numFrames = static_cast<int>((numSamples - fftSize) / hopSize) + 1;
    std::vector<double> flux;
    flux.reserve(numFrames);
    std::vector<float> prevMag(fftSize / 2 + 1, 0.0f);

    for (int frame = 0; frame < numFrames; ++frame) {
        size_t offset = frame * hopSize;
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

    double hopDuration = static_cast<double>(hopSize) / sampleRate;
    int minLag = static_cast<int>(60.0 / maxBPM_ / hopDuration);
    int maxLag = static_cast<int>(60.0 / minBPM_ / hopDuration);
    maxLag = std::min(maxLag, static_cast<int>(flux.size()) / 2);

    double mean = 0.0;
    for (auto& v : flux) mean += v;
    mean /= flux.size();

    double bestBPM = 0.0;
    double bestVal = -1e10;

    for (int lag = minLag; lag <= maxLag; ++lag) {
        double sum = 0.0;
        int count = 0;
        for (int i = 0; i + lag < static_cast<int>(flux.size()); ++i) {
            sum += (flux[i] - mean) * (flux[i + lag] - mean);
            count++;
        }
        double val = (count > 0) ? sum / count : 0.0;

        double bpm = 60.0 / (lag * hopDuration);

        // Boost common DJ ranges
        if (bpm >= 120 && bpm <= 135) val *= 1.1;
        if (bpm >= 125 && bpm <= 130) val *= 1.05;

        int doubleLag = lag / 2;
        if (doubleLag >= minLag) {
            double sum2 = 0.0;
            int count2 = 0;
            for (int i = 0; i + doubleLag < static_cast<int>(flux.size()); ++i) {
                sum2 += (flux[i] - mean) * (flux[i + doubleLag] - mean);
                count2++;
            }
            if (count2 > 0) val += (sum2 / count2) * 0.5;
        }

        if (val > bestVal) {
            bestVal = val;
            bestBPM = bpm;
        }
    }

    return bestBPM;
}

double UltraPrecisionBPMService::combFilterPass(const float* mono, size_t numSamples,
                                                  int sampleRate, double coarseBPM) {
    double bestBPM = coarseBPM;
    double bestEnergy = -1.0;

    double searchMin = std::max(minBPM_, coarseBPM - 5.0);
    double searchMax = std::min(maxBPM_, coarseBPM + 5.0);

    int hopSize = 256;
    int fftSize = 1024;
    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    int numFrames = static_cast<int>((numSamples - fftSize) / hopSize) + 1;
    std::vector<double> envelope;
    envelope.reserve(numFrames);
    std::vector<float> prevMag(fftSize / 2 + 1, 0.0f);

    for (int frame = 0; frame < numFrames; ++frame) {
        size_t offset = frame * hopSize;
        if (offset + fftSize > numSamples) break;

        std::vector<std::complex<float>> spectrum;
        fft.forward(mono + offset, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        double sf = 0.0;
        for (size_t bin = 0; bin < mag.size(); ++bin) {
            double diff = mag[bin] - prevMag[bin];
            if (diff > 0) sf += diff;
        }
        envelope.push_back(sf);
        prevMag = mag;
    }

    double hopDuration = static_cast<double>(hopSize) / sampleRate;

    for (double testBPM = searchMin; testBPM <= searchMax; testBPM += 0.01) {
        double period = 60.0 / testBPM / hopDuration; // in frames
        double energy = 0.0;
        int count = 0;

        for (double pos = 0; pos < envelope.size(); pos += period) {
            int idx = static_cast<int>(pos);
            if (idx >= 0 && idx < static_cast<int>(envelope.size())) {
                energy += envelope[idx];
                count++;
            }
        }

        if (count > 0) energy /= count;
        if (energy > bestEnergy) {
            bestEnergy = energy;
            bestBPM = testBPM;
        }
    }

    return bestBPM;
}

double UltraPrecisionBPMService::crossCorrelationPass(const float* mono, size_t numSamples,
                                                        int sampleRate, double refinedBPM) {
    double beatPeriodSamples = 60.0 / refinedBPM * sampleRate;
    int windowSize = static_cast<int>(beatPeriodSamples * 0.5);
    if (windowSize < 256) windowSize = 256;

    std::vector<double> intervals;

    int numSegments = std::min(32, static_cast<int>(numSamples / beatPeriodSamples) - 2);
    for (int seg = 1; seg < numSegments; ++seg) {
        size_t refStart = static_cast<size_t>(seg * beatPeriodSamples);
        if (refStart + windowSize * 3 >= numSamples) break;

        int searchRadius = static_cast<int>(beatPeriodSamples * 0.1);
        double bestCorr = -1e10;
        int bestOffset = static_cast<int>(beatPeriodSamples);

        for (int d = -searchRadius; d <= searchRadius; ++d) {
            int testOffset = static_cast<int>(beatPeriodSamples) + d;
            if (refStart + testOffset + windowSize >= numSamples) continue;

            double corr = 0.0;
            for (int i = 0; i < windowSize; ++i) {
                corr += mono[refStart + i] * mono[refStart + testOffset + i];
            }
            if (corr > bestCorr) {
                bestCorr = corr;
                bestOffset = testOffset;
            }
        }

        double intervalSec = static_cast<double>(bestOffset) / sampleRate;
        double intervalBPM = 60.0 / intervalSec;
        if (intervalBPM >= minBPM_ && intervalBPM <= maxBPM_) {
            intervals.push_back(intervalBPM);
        }
    }

    if (intervals.empty()) return refinedBPM;

    std::sort(intervals.begin(), intervals.end());
    return intervals[intervals.size() / 2];
}

double UltraPrecisionBPMService::beatIntervalPass(const std::vector<double>& beats) {
    if (beats.size() < 4) return 0.0;

    std::vector<double> intervals;
    for (size_t i = 1; i < beats.size(); ++i) {
        double interval = beats[i] - beats[i - 1];
        if (interval > 0.2 && interval < 2.0) {
            intervals.push_back(60.0 / interval);
        }
    }

    if (intervals.empty()) return 0.0;

    std::sort(intervals.begin(), intervals.end());

    double median = intervals[intervals.size() / 2];
    std::vector<double> filtered;
    for (auto& v : intervals) {
        if (std::fabs(v - median) / median < 0.02) {
            filtered.push_back(v);
        }
    }

    if (filtered.empty()) return median;

    double sum = 0.0;
    for (auto& v : filtered) sum += v;
    return sum / filtered.size();
}

double UltraPrecisionBPMService::weightedMedian(const std::vector<double>& bpms,
                                                   const std::vector<double>& weights) {
    if (bpms.empty()) return 0.0;
    if (bpms.size() == 1) return bpms[0];

    std::vector<std::pair<double, double>> pairs;
    for (size_t i = 0; i < bpms.size(); ++i) {
        pairs.emplace_back(bpms[i], i < weights.size() ? weights[i] : 1.0);
    }
    std::sort(pairs.begin(), pairs.end());

    double totalWeight = 0.0;
    for (auto& p : pairs) totalWeight += p.second;
    double halfWeight = totalWeight / 2.0;

    double cumWeight = 0.0;
    for (auto& p : pairs) {
        cumWeight += p.second;
        if (cumWeight >= halfWeight) return p.first;
    }
    return pairs.back().first;
}

double UltraPrecisionBPMService::snapToGrid(double bpm) {
    double rounded = std::round(bpm * 10.0) / 10.0;

    double intBPM = std::round(bpm);
    if (std::fabs(bpm - intBPM) < 0.15) {
        return intBPM;
    }

    double halfBPM = std::round(bpm * 2.0) / 2.0;
    if (std::fabs(bpm - halfBPM) < 0.08) {
        return halfBPM;
    }

    return rounded;
}

UltraPrecisionBPMResult UltraPrecisionBPMService::detect(const AudioTrack& track) {
    spdlog::info("UltraPrecisionBPMService: analyzing {} ({:.1f}s, {} passes)",
                 track.getFilePath(), track.getDuration(), numPasses_);

    UltraPrecisionBPMResult result;
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    if (numSamples == 0) {
        spdlog::warn("UltraPrecisionBPMService: empty track");
        return result;
    }

    double pass1 = coarsePass(data, numSamples, sr);
    result.passBPMs.push_back(pass1);
    spdlog::debug("UltraPrecisionBPMService: pass1 coarse = {:.2f}", pass1);

    double pass2 = pass1;
    if (numPasses_ >= 2) {
        pass2 = combFilterPass(data, numSamples, sr, pass1);
        result.passBPMs.push_back(pass2);
        spdlog::debug("UltraPrecisionBPMService: pass2 comb = {:.2f}", pass2);
    }

    double pass3 = pass2;
    if (numPasses_ >= 3) {
        pass3 = crossCorrelationPass(data, numSamples, sr, pass2);
        result.passBPMs.push_back(pass3);
        spdlog::debug("UltraPrecisionBPMService: pass3 xcorr = {:.2f}", pass3);
    }

    BPMDetector basicDetector;
    basicDetector.setMinBPM(minBPM_);
    basicDetector.setMaxBPM(maxBPM_);
    auto basicResult = basicDetector.detect(track);
    result.beats = basicResult.beats;
    result.offset = basicResult.offset;

    double pass4 = pass3;
    if (numPasses_ >= 4 && !result.beats.empty()) {
        pass4 = beatIntervalPass(result.beats);
        if (pass4 > 0) {
            result.passBPMs.push_back(pass4);
            spdlog::debug("UltraPrecisionBPMService: pass4 interval = {:.2f}", pass4);
        }
    }

    std::vector<double> weights;
    for (size_t i = 0; i < result.passBPMs.size(); ++i) {
        weights.push_back(1.0 + i * 0.5);
    }

    double finalBPM = weightedMedian(result.passBPMs, weights);
    result.bpm = snapToGrid(finalBPM);
    result.passCount = static_cast<int>(result.passBPMs.size());

    if (result.passBPMs.size() > 1) {
        double mean = 0.0;
        for (auto& v : result.passBPMs) mean += v;
        mean /= result.passBPMs.size();
        double var = 0.0;
        for (auto& v : result.passBPMs) var += (v - mean) * (v - mean);
        result.bpmVariance = var / result.passBPMs.size();
    }

    result.confidence = std::clamp(1.0 - result.bpmVariance * 10.0, 0.0, 1.0);

    spdlog::info("UltraPrecisionBPMService: final {:.2f} BPM (confidence: {:.0f}%, variance: {:.4f})",
                 result.bpm, result.confidence * 100, result.bpmVariance);
    return result;
}

} // namespace BeatMate::Core
