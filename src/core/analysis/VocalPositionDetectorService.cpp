#include "VocalPositionDetectorService.h"
#include "../audio/AudioTrack.h"
#include "BPMDetector.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

VocalPositionDetectorService::VocalPositionDetectorService() = default;
VocalPositionDetectorService::~VocalPositionDetectorService() = default;

float VocalPositionDetectorService::detectPitch(const float* data, int numSamples, int sampleRate) {
    // YIN-like pitch detection
    int minLag = static_cast<int>(sampleRate / maxPitch_);
    int maxLag = static_cast<int>(sampleRate / minPitch_);
    maxLag = std::min(maxLag, numSamples / 2);

    if (minLag >= maxLag) return 0.0f;

    std::vector<float> d(maxLag + 1, 0.0f);
    for (int lag = 1; lag <= maxLag; ++lag) {
        float sum = 0.0f;
        for (int i = 0; i + lag < numSamples; ++i) {
            float diff = data[i] - data[i + lag];
            sum += diff * diff;
        }
        d[lag] = sum;
    }

    // Cumulative mean normalized difference function
    std::vector<float> dn(maxLag + 1, 1.0f);
    float runningSum = 0.0f;
    for (int lag = 1; lag <= maxLag; ++lag) {
        runningSum += d[lag];
        if (runningSum > 0) {
            dn[lag] = d[lag] * lag / runningSum;
        }
    }

    float threshold = 0.15f;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        if (dn[lag] < threshold) {
            // Parabolic interpolation
            if (lag > 0 && lag < maxLag) {
                float a = dn[lag - 1];
                float b = dn[lag];
                float c = dn[lag + 1];
                float delta = 0.5f * (a - c) / (a - 2.0f * b + c);
                float refinedLag = lag + delta;
                return sampleRate / refinedLag;
            }
            return static_cast<float>(sampleRate) / lag;
        }
    }

    return 0.0f; // No pitch detected (unvoiced)
}

float VocalPositionDetectorService::computeVoicedProbability(const float* data, int numSamples,
                                                               int sampleRate) {
    int zeroCrossings = 0;
    for (int i = 1; i < numSamples; ++i) {
        if ((data[i] > 0) != (data[i - 1] > 0)) zeroCrossings++;
    }
    float zcr = static_cast<float>(zeroCrossings) / numSamples * sampleRate;
    // Voiced speech: ZCR < 3000, unvoiced: > 3000
    float zcrScore = std::clamp(1.0f - zcr / 5000.0f, 0.0f, 1.0f);

    float energy = 0.0f;
    for (int i = 0; i < numSamples; ++i) energy += data[i] * data[i];
    energy = std::sqrt(energy / numSamples);
    float energyScore = std::clamp(energy * 10.0f, 0.0f, 1.0f);

    float pitch = detectPitch(data, numSamples, sampleRate);
    float pitchScore = (pitch > 0) ? 0.8f : 0.2f;

    return zcrScore * 0.3f + energyScore * 0.3f + pitchScore * 0.4f;
}

VocalPositionResult VocalPositionDetectorService::detect(const AudioTrack& track, double bpm) {
    spdlog::info("VocalPositionDetectorService: analyzing {}", track.getFilePath());

    VocalPositionResult result;

    if (bpm <= 0) {
        BPMDetector detector;
        bpm = detector.detect(track).bpm;
    }

    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    int frameSize = static_cast<int>(sr * 0.03); // 30ms frames
    int hopSize = frameSize / 2;
    result.frameDuration = static_cast<double>(hopSize) / sr;

    int numFrames = static_cast<int>((numSamples - frameSize) / hopSize) + 1;

    result.pitchCurve.reserve(numFrames);
    result.voicedCurve.reserve(numFrames);

    for (int frame = 0; frame < numFrames; ++frame) {
        size_t offset = frame * hopSize;
        if (offset + frameSize > numSamples) break;

        float pitch = detectPitch(data + offset, frameSize, sr);
        float voiced = computeVoicedProbability(data + offset, frameSize, sr);

        result.pitchCurve.push_back(pitch);
        result.voicedCurve.push_back(voiced);
    }

    float vocalThreshold = 0.6f;
    double minDuration = 0.3; // 300ms minimum
    bool inVocal = false;
    VocalPosition current;

    double barDuration = 240.0 / bpm; // 4 beats
    result.totalBars = static_cast<int>(track.getDuration() / barDuration);

    for (int f = 0; f < static_cast<int>(result.voicedCurve.size()); ++f) {
        double time = f * result.frameDuration;

        if (result.voicedCurve[f] >= vocalThreshold && !inVocal) {
            current = {};
            current.startTime = time;
            current.barStart = static_cast<int>(time / barDuration);
            inVocal = true;
        } else if ((result.voicedCurve[f] < vocalThreshold || f == static_cast<int>(result.voicedCurve.size()) - 1) && inVocal) {
            current.endTime = time;
            current.barEnd = static_cast<int>(time / barDuration);
            inVocal = false;

            if (current.endTime - current.startTime >= minDuration) {
                float maxEnergy = 0.0f;
                double peakTime = current.startTime;
                float pitchSum = 0.0f;
                int pitchCount = 0;

                int startFrame = static_cast<int>(current.startTime / result.frameDuration);
                int endFrame = static_cast<int>(current.endTime / result.frameDuration);

                for (int ff = startFrame; ff <= endFrame && ff < static_cast<int>(result.voicedCurve.size()); ++ff) {
                    if (result.voicedCurve[ff] > maxEnergy) {
                        maxEnergy = result.voicedCurve[ff];
                        peakTime = ff * result.frameDuration;
                    }
                    if (result.pitchCurve[ff] > 0) {
                        pitchSum += result.pitchCurve[ff];
                        pitchCount++;
                    }
                }

                current.peakTime = peakTime;
                current.peakEnergy = maxEnergy;
                current.averagePitch = (pitchCount > 0) ? pitchSum / pitchCount : 0.0f;
                current.label = "Vocal";

                result.positions.push_back(current);
            }
        }
    }

    std::vector<bool> barHasVocal(result.totalBars + 1, false);
    for (auto& pos : result.positions) {
        for (int bar = pos.barStart; bar <= pos.barEnd && bar < result.totalBars; ++bar) {
            barHasVocal[bar] = true;
        }
    }
    result.totalVocalBars = static_cast<int>(std::count(barHasVocal.begin(), barHasVocal.end(), true));

    spdlog::info("VocalPositionDetectorService: {} vocal regions, {}/{} bars",
                 result.positions.size(), result.totalVocalBars, result.totalBars);
    return result;
}

} // namespace BeatMate::Core
