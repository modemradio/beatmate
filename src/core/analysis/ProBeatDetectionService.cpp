#include "ProBeatDetectionService.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

ProBeatDetectionService::ProBeatDetectionService() = default;
ProBeatDetectionService::~ProBeatDetectionService() = default;

std::vector<double> ProBeatDetectionService::computeSubBandOnsets(
    const float* mono, size_t numSamples, int sampleRate, float freqLow, float freqHigh) {

    int fftSize = 2048;
    int hopSize = 256;
    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    int binLow = static_cast<int>(freqLow * fftSize / sampleRate);
    int binHigh = static_cast<int>(freqHigh * fftSize / sampleRate);
    binLow = std::max(1, binLow);
    binHigh = std::min(fftSize / 2, binHigh);

    int numFrames = static_cast<int>((numSamples - fftSize) / hopSize) + 1;
    std::vector<double> onset;
    onset.reserve(numFrames);

    std::vector<float> prevMag(fftSize / 2 + 1, 0.0f);

    for (int frame = 0; frame < numFrames; ++frame) {
        size_t offset = frame * hopSize;
        if (offset + fftSize > numSamples) break;

        std::vector<std::complex<float>> spectrum;
        fft.forward(mono + offset, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        double sf = 0.0;
        for (int bin = binLow; bin <= binHigh && bin < static_cast<int>(mag.size()); ++bin) {
            double diff = mag[bin] - prevMag[bin];
            if (diff > 0) sf += diff;
        }
        onset.push_back(sf);
        prevMag = mag;
    }

    double maxVal = 0.0;
    for (auto& v : onset) maxVal = std::max(maxVal, v);
    if (maxVal > 0) {
        for (auto& v : onset) v /= maxVal;
    }

    return onset;
}

BeatType ProBeatDetectionService::classifyBeat(const float* mono, size_t position, int sampleRate) {
    int windowSize = 1024;
    if (position + windowSize >= static_cast<size_t>(sampleRate * 100)) return BeatType::Other;

    FFTProcessor fft(windowSize);
    fft.setWindow(WindowType::Hann);

    std::vector<std::complex<float>> spectrum;
    fft.forward(mono + position, spectrum);
    auto mag = fft.getMagnitudes(spectrum);

    float kickEnergy = 0.0f, snareEnergy = 0.0f, hihatEnergy = 0.0f;
    float totalEnergy = 0.0f;

    for (int bin = 0; bin < static_cast<int>(mag.size()); ++bin) {
        float freq = static_cast<float>(bin) * sampleRate / windowSize;
        float e = mag[bin] * mag[bin];
        totalEnergy += e;

        if (freq >= kickLow_ && freq <= kickHigh_) kickEnergy += e;
        else if (freq >= snareLow_ && freq <= snareHigh_) snareEnergy += e;
        else if (freq >= hihatLow_ && freq <= hihatHigh_) hihatEnergy += e;
    }

    if (totalEnergy < 1e-10f) return BeatType::Other;

    float kickRatio = kickEnergy / totalEnergy;
    float snareRatio = snareEnergy / totalEnergy;
    float hihatRatio = hihatEnergy / totalEnergy;

    if (kickRatio > 0.4f && kickRatio > snareRatio && kickRatio > hihatRatio)
        return BeatType::Kick;
    if (snareRatio > 0.3f && snareRatio > hihatRatio)
        return BeatType::Snare;
    if (hihatRatio > 0.2f)
        return BeatType::HiHat;

    return BeatType::Other;
}

std::string ProBeatDetectionService::detectPattern(const std::vector<DetectedBeat>& beats, double bpm) {
    if (beats.empty() || bpm <= 0) return "";

    double beatDuration = 60.0 / bpm;
    int subdivisions = 4; // 16th notes
    double subDuration = beatDuration / subdivisions;

    int stepsPerBar = 16;
    std::string pattern(stepsPerBar, '.');

    for (auto& beat : beats) {
        double barPos = std::fmod(beat.position, beatDuration * 4.0);
        int step = static_cast<int>(barPos / subDuration + 0.5) % stepsPerBar;

        char c = '.';
        switch (beat.type) {
            case BeatType::Kick:  c = 'K'; break;
            case BeatType::Snare: c = 'S'; break;
            case BeatType::HiHat: c = 'H'; break;
            default: c = 'x'; break;
        }

        if (pattern[step] == '.' || beat.strength > 0.5f) {
            pattern[step] = c;
        }
    }

    return pattern;
}

ProBeatDetectionResult ProBeatDetectionService::detect(const AudioTrack& track) {
    spdlog::info("ProBeatDetectionService: analyzing {}", track.getFilePath());

    ProBeatDetectionResult result;
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    if (numSamples == 0) return result;

    auto kickOnsets = computeSubBandOnsets(data, numSamples, sr, kickLow_, kickHigh_);
    auto snareOnsets = computeSubBandOnsets(data, numSamples, sr, snareLow_, snareHigh_);
    auto hihatOnsets = computeSubBandOnsets(data, numSamples, sr, hihatLow_, hihatHigh_);

    int hopSize = 256;
    double hopDuration = static_cast<double>(hopSize) / sr;

    auto pickPeaks = [&](const std::vector<double>& onset, BeatType type) {
        double minInterval = 0.05; // 50ms minimum
        int minFrames = static_cast<int>(minInterval / hopDuration);
        int lastPeak = -minFrames;

        for (int i = 1; i < static_cast<int>(onset.size()) - 1; ++i) {
            if (onset[i] > threshold_ && onset[i] > onset[i - 1] && onset[i] > onset[i + 1]) {
                if (i - lastPeak >= minFrames) {
                    DetectedBeat beat;
                    beat.position = i * hopDuration;
                    beat.type = type;
                    beat.strength = static_cast<float>(onset[i]);
                    beat.confidence = static_cast<float>(std::min(1.0, onset[i] * 1.5));
                    result.beats.push_back(beat);
                    lastPeak = i;

                    switch (type) {
                        case BeatType::Kick: result.kickCount++; break;
                        case BeatType::Snare: result.snareCount++; break;
                        case BeatType::HiHat: result.hihatCount++; break;
                        default: break;
                    }
                }
            }
        }
    };

    pickPeaks(kickOnsets, BeatType::Kick);
    pickPeaks(snareOnsets, BeatType::Snare);
    pickPeaks(hihatOnsets, BeatType::HiHat);

    std::sort(result.beats.begin(), result.beats.end(),
              [](const DetectedBeat& a, const DetectedBeat& b) { return a.position < b.position; });

    std::vector<double> kickPositions;
    for (auto& b : result.beats) {
        if (b.type == BeatType::Kick) kickPositions.push_back(b.position);
    }
    if (kickPositions.size() > 4) {
        std::vector<double> intervals;
        for (size_t i = 1; i < kickPositions.size(); ++i) {
            double interval = kickPositions[i] - kickPositions[i - 1];
            if (interval > 0.2 && interval < 2.0) intervals.push_back(interval);
        }
        if (!intervals.empty()) {
            std::sort(intervals.begin(), intervals.end());
            double medianInterval = intervals[intervals.size() / 2];
            result.bpm = 60.0 / medianInterval;
        }
    }

    result.pattern = detectPattern(result.beats, result.bpm);

    spdlog::info("ProBeatDetectionService: {} beats (K:{} S:{} H:{}), pattern: {}",
                 result.beats.size(), result.kickCount, result.snareCount,
                 result.hihatCount, result.pattern);
    return result;
}

} // namespace BeatMate::Core
