#include "UltraPreciseHotcueService.h"
#include "../audio/AudioTrack.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

UltraPreciseHotcueService::UltraPreciseHotcueService() = default;
UltraPreciseHotcueService::~UltraPreciseHotcueService() = default;

double UltraPreciseHotcueService::findExactZeroCrossing(const float* data, size_t crossingIndex) {
    float s0 = data[crossingIndex];
    float s1 = data[crossingIndex + 1];

    if (std::fabs(s1 - s0) < 1e-10f) return static_cast<double>(crossingIndex);

    double fraction = -s0 / (s1 - s0);
    return crossingIndex + fraction;
}

double UltraPreciseHotcueService::findOnset(const float* data, size_t startSample,
                                               size_t endSample, int sampleRate) {
    int windowSize = sampleRate / 1000; // 1ms windows
    if (windowSize < 4) windowSize = 4;

    double maxDerivative = 0.0;
    size_t onsetSample = startSample;

    for (size_t i = startSample + windowSize; i + windowSize < endSample; i += windowSize / 2) {
        double energyCurrent = 0.0;
        double energyPrev = 0.0;

        for (int j = 0; j < windowSize; ++j) {
            energyCurrent += data[i + j] * data[i + j];
            energyPrev += data[i - windowSize + j] * data[i - windowSize + j];
        }

        double derivative = energyCurrent - energyPrev;
        if (derivative > maxDerivative) {
            maxDerivative = derivative;
            onsetSample = i;
        }
    }

    return static_cast<double>(onsetSample) / sampleRate;
}

PreciseCuePoint UltraPreciseHotcueService::snapToBeat(double position,
                                                         const std::vector<double>& beats, double bpm) {
    PreciseCuePoint result;
    result.cue.position = position;

    if (beats.empty()) {
        result.sampleAccuratePosition = position;
        return result;
    }

    double nearestBeat = beats[0];
    double minDist = std::fabs(beats[0] - position);

    for (size_t i = 1; i < beats.size(); ++i) {
        double dist = std::fabs(beats[i] - position);
        if (dist < minDist) {
            minDist = dist;
            nearestBeat = beats[i];
        }
    }

    result.sampleAccuratePosition = nearestBeat;
    result.beatAlignedPosition = nearestBeat;
    result.cue.position = nearestBeat;

    if (bpm <= 0.0) {
        result.beatProximity = 0.0f;
        result.beatNumber = 0;
        result.barNumber = 0;
        result.barAlignedPosition = nearestBeat;
        return result;
    }

    double beatDuration = 60.0 / bpm;
    result.beatProximity = static_cast<float>(1.0 - std::min(minDist / (beatDuration * 0.5), 1.0));

    double beatsFromStart = nearestBeat / beatDuration;
    result.beatNumber = (static_cast<int>(beatsFromStart) % 4) + 1;
    result.barNumber = static_cast<int>(beatsFromStart / 4) + 1;

    double barDuration = 240.0 / bpm;
    result.barAlignedPosition = std::round(nearestBeat / barDuration) * barDuration;

    return result;
}

PreciseCuePoint UltraPreciseHotcueService::snapToBar(double position,
                                                        const std::vector<double>& beats, double bpm) {
    auto result = snapToBeat(position, beats, bpm);

    if (bpm > 0) {
        double barDuration = 240.0 / bpm;
        double barPosition = std::round(position / barDuration) * barDuration;

        double nearestBeat = barPosition;
        double minDist = 1e10;
        for (double beat : beats) {
            double dist = std::fabs(beat - barPosition);
            if (dist < minDist) {
                minDist = dist;
                nearestBeat = beat;
            }
        }

        result.cue.position = nearestBeat;
        result.sampleAccuratePosition = nearestBeat;
        result.beatAlignedPosition = nearestBeat;
        result.barAlignedPosition = nearestBeat;
        result.beatNumber = 1; // Downbeat
    }

    return result;
}

PreciseCuePoint UltraPreciseHotcueService::snapToZeroCrossing(const AudioTrack& track, double position) {
    PreciseCuePoint result;

    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    size_t samplePos = static_cast<size_t>(position * sr);
    if (samplePos >= numSamples - 1) {
        result.cue.position = position;
        result.sampleAccuratePosition = position;
        return result;
    }

    int searchRadius = sr / 200; // 5ms
    size_t searchStart = (samplePos > static_cast<size_t>(searchRadius)) ? samplePos - searchRadius : 0;
    size_t searchEnd = std::min(samplePos + searchRadius, numSamples - 2);

    double bestDist = 1e10;
    size_t bestCrossing = samplePos;

    for (size_t i = searchStart; i < searchEnd; ++i) {
        if ((data[i] >= 0 && data[i + 1] < 0) || (data[i] < 0 && data[i + 1] >= 0)) {
            double exactPos = findExactZeroCrossing(data, i);
            double dist = std::fabs(exactPos - static_cast<double>(samplePos));
            if (dist < bestDist) {
                bestDist = dist;
                bestCrossing = i;
            }
        }
    }

    double exactSamplePos = findExactZeroCrossing(data, bestCrossing);
    result.sampleAccuratePosition = exactSamplePos / sr;
    result.cue.position = result.sampleAccuratePosition;

    return result;
}

PreciseCuePoint UltraPreciseHotcueService::snapToTransient(const AudioTrack& track,
                                                              double position, double searchMs) {
    PreciseCuePoint result;

    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    size_t samplePos = static_cast<size_t>(position * sr);
    size_t searchRadius = static_cast<size_t>(searchMs / 1000.0 * sr);
    size_t searchStart = (samplePos > searchRadius) ? samplePos - searchRadius : 0;
    size_t searchEnd = std::min(samplePos + searchRadius, numSamples);

    double onsetPos = findOnset(data, searchStart, searchEnd, sr);

    result.cue.position = onsetPos;
    result.sampleAccuratePosition = onsetPos;

    auto zeroCrossed = snapToZeroCrossing(track, onsetPos);
    if (std::fabs(zeroCrossed.sampleAccuratePosition - onsetPos) < 0.002) { // Within 2ms
        result.sampleAccuratePosition = zeroCrossed.sampleAccuratePosition;
        result.cue.position = result.sampleAccuratePosition;
    }

    return result;
}

PreciseCuePoint UltraPreciseHotcueService::refine(const AudioTrack& track, const CuePoint& cue,
                                                     const std::vector<double>& beats, double bpm) {
    // Multi-step refinement: beat -> transient -> zero crossing
    auto beatSnapped = snapToBeat(cue.position, beats, bpm);

    auto transientSnapped = snapToTransient(track, beatSnapped.cue.position, 20.0);

    auto result = snapToZeroCrossing(track, transientSnapped.sampleAccuratePosition);
    result.cue = cue;
    result.cue.position = result.sampleAccuratePosition;
    result.beatAlignedPosition = beatSnapped.beatAlignedPosition;
    result.barAlignedPosition = beatSnapped.barAlignedPosition;
    result.beatProximity = beatSnapped.beatProximity;
    result.beatNumber = beatSnapped.beatNumber;
    result.barNumber = beatSnapped.barNumber;

    spdlog::debug("UltraPreciseHotcueService: refined cue {} from {:.6f}s to {:.6f}s",
                  cue.number, cue.position, result.cue.position);
    return result;
}

std::vector<PreciseCuePoint> UltraPreciseHotcueService::refineAll(
    const AudioTrack& track, const std::vector<CuePoint>& cues,
    const std::vector<double>& beats, double bpm) {

    spdlog::info("UltraPreciseHotcueService: refining {} cues", cues.size());

    std::vector<PreciseCuePoint> results;
    results.reserve(cues.size());

    for (auto& cue : cues) {
        results.push_back(refine(track, cue, beats, bpm));
    }

    return results;
}

} // namespace BeatMate::Core
