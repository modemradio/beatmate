#include "AutoTempoDetectionService.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

AutoTempoDetectionService::AutoTempoDetectionService() = default;
AutoTempoDetectionService::~AutoTempoDetectionService() = default;

double AutoTempoDetectionService::estimateSegmentBPM(const float* data, size_t numSamples, int sampleRate) {
    int fftSize = 1024;
    int hopSize = 256;
    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    int numFrames = static_cast<int>((numSamples - fftSize) / hopSize) + 1;
    if (numFrames < 4) return 0.0;

    std::vector<double> flux;
    flux.reserve(numFrames);
    std::vector<float> prevMag(fftSize / 2 + 1, 0.0f);

    for (int frame = 0; frame < numFrames; ++frame) {
        size_t offset = frame * hopSize;
        if (offset + fftSize > numSamples) break;

        std::vector<std::complex<float>> spectrum;
        fft.forward(data + offset, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        double sf = 0.0;
        for (size_t bin = 0; bin < mag.size(); ++bin) {
            double diff = mag[bin] - prevMag[bin];
            if (diff > 0) sf += diff;
        }
        flux.push_back(sf);
        prevMag = mag;
    }

    // Autocorrelation
    double hopDuration = static_cast<double>(hopSize) / sampleRate;
    int minLag = static_cast<int>(60.0 / maxBPM_ / hopDuration);
    int maxLag = static_cast<int>(60.0 / minBPM_ / hopDuration);
    maxLag = std::min(maxLag, static_cast<int>(flux.size()) / 2);

    if (minLag >= maxLag) return 0.0;

    double mean = std::accumulate(flux.begin(), flux.end(), 0.0) / flux.size();

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

        if (val > bestVal) {
            bestVal = val;
            bestBPM = 60.0 / (lag * hopDuration);
        }
    }

    return bestBPM;
}

std::vector<TempoSegment> AutoTempoDetectionService::mergeSegments(
    const std::vector<TempoSegment>& segments, double tolerance) {

    if (segments.empty()) return {};

    std::vector<TempoSegment> merged;
    TempoSegment current = segments[0];

    for (size_t i = 1; i < segments.size(); ++i) {
        double diff = std::fabs(segments[i].bpm - current.bpm) / current.bpm * 100.0;
        if (diff < tolerance) {
            double totalDuration = segments[i].endTime - current.startTime;
            double currentDuration = current.endTime - current.startTime;
            double newDuration = segments[i].endTime - segments[i].startTime;
            current.bpm = (current.bpm * currentDuration + segments[i].bpm * newDuration) / totalDuration;
            current.endTime = segments[i].endTime;
            current.confidence = std::max(current.confidence, segments[i].confidence);
        } else {
            merged.push_back(current);
            current = segments[i];
        }
    }
    merged.push_back(current);

    return merged;
}

TempoMap AutoTempoDetectionService::detect(const AudioTrack& track) {
    spdlog::info("AutoTempoDetectionService: analyzing {} ({:.1f}s)", track.getFilePath(), track.getDuration());

    TempoMap result;
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t totalSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    if (totalSamples == 0) return result;

    size_t segmentSamples = static_cast<size_t>(segmentDuration_ * sr);
    size_t numSegments = totalSamples / segmentSamples;

    if (numSegments == 0) {
        // Track shorter than one segment
        TempoSegment seg;
        seg.startTime = 0.0;
        seg.endTime = track.getDuration();
        seg.bpm = estimateSegmentBPM(data, totalSamples, sr);
        seg.confidence = 0.5f;
        result.segments.push_back(seg);
        result.averageBPM = seg.bpm;
        result.minBPM = seg.bpm;
        result.maxBPM = seg.bpm;
        return result;
    }

    std::vector<TempoSegment> rawSegments;
    for (size_t i = 0; i < numSegments; ++i) {
        const float* segData = data + i * segmentSamples;
        size_t segLen = std::min(segmentSamples, totalSamples - i * segmentSamples);

        TempoSegment seg;
        seg.startTime = i * segmentDuration_;
        seg.endTime = seg.startTime + segmentDuration_;
        seg.bpm = estimateSegmentBPM(segData, segLen, sr);

        if (seg.bpm < minBPM_ || seg.bpm > maxBPM_) {
            seg.confidence = 0.1f;
        } else {
            seg.confidence = 0.8f;
        }

        rawSegments.push_back(seg);
    }

    result.segments = mergeSegments(rawSegments, variationThreshold_);

    std::vector<double> validBPMs;
    for (auto& seg : result.segments) {
        if (seg.bpm >= minBPM_ && seg.bpm <= maxBPM_) {
            validBPMs.push_back(seg.bpm);
        }
    }

    if (!validBPMs.empty()) {
        result.minBPM = *std::min_element(validBPMs.begin(), validBPMs.end());
        result.maxBPM = *std::max_element(validBPMs.begin(), validBPMs.end());

        double sum = std::accumulate(validBPMs.begin(), validBPMs.end(), 0.0);
        result.averageBPM = sum / validBPMs.size();

        result.tempoVariation = (result.maxBPM - result.minBPM) / result.averageBPM * 100.0;
        result.isVariableTempo = result.tempoVariation > variationThreshold_;
    }

    spdlog::info("AutoTempoDetectionService: {} segments, avg {:.1f} BPM, variation {:.1f}%, variable: {}",
                 result.segments.size(), result.averageBPM, result.tempoVariation,
                 result.isVariableTempo ? "yes" : "no");
    return result;
}

} // namespace BeatMate::Core
