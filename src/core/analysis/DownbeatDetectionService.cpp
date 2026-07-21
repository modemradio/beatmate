#include "DownbeatDetectionService.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

DownbeatDetectionService::DownbeatDetectionService() = default;
DownbeatDetectionService::~DownbeatDetectionService() = default;

std::vector<float> DownbeatDetectionService::computeAccentPattern(
    const AudioTrack& track, const std::vector<double>& beats) {

    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    int fftSize = 2048;
    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    std::vector<float> accents;
    accents.reserve(beats.size());

    for (double beatPos : beats) {
        size_t samplePos = static_cast<size_t>(beatPos * sr);
        if (samplePos + fftSize >= numSamples) {
            accents.push_back(0.0f);
            continue;
        }

        // Compute energy in bass range (downbeats typically have stronger bass)
        std::vector<std::complex<float>> spectrum;
        fft.forward(data + samplePos, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        float bassEnergy = 0.0f;
        float totalEnergy = 0.0f;
        for (int bin = 0; bin < static_cast<int>(mag.size()); ++bin) {
            float freq = static_cast<float>(bin) * sr / fftSize;
            float energy = mag[bin] * mag[bin];
            totalEnergy += energy;
            if (freq >= 30.0f && freq <= 200.0f) {
                bassEnergy += energy;
            }
        }

        // Also compute transient strength (RMS of first 10ms)
        int transientSamples = std::min(static_cast<int>(sr * 0.01), static_cast<int>(numSamples - samplePos));
        float transientRMS = 0.0f;
        for (int i = 0; i < transientSamples; ++i) {
            transientRMS += data[samplePos + i] * data[samplePos + i];
        }
        transientRMS = std::sqrt(transientRMS / transientSamples);

        // Combined accent score: bass energy + transient strength
        float accent = bassEnergy * 0.6f + transientRMS * 0.4f;
        accents.push_back(accent);
    }

    float maxAccent = 0.0f;
    for (auto& a : accents) maxAccent = std::max(maxAccent, a);
    if (maxAccent > 0) {
        for (auto& a : accents) a /= maxAccent;
    }

    return accents;
}

float DownbeatDetectionService::scorePhase(const std::vector<float>& accents, int phase, int beatsPerBar) {
    // In 4/4 time, beat 1 (downbeat) should have the strongest accent
    float expectedWeights[4] = { 1.0f, 0.2f, 0.6f, 0.2f };

    float score = 0.0f;
    int count = 0;

    for (int i = phase; i < static_cast<int>(accents.size()); i += beatsPerBar) {
        for (int b = 0; b < beatsPerBar && (i + b) < static_cast<int>(accents.size()); ++b) {
            float expected = expectedWeights[b % 4];
            float actual = accents[i + b];

            // Score increases when strong beats have high accent and weak beats have low
            score += actual * expected - (1.0f - actual) * (1.0f - expected) * 0.5f;
            count++;
        }
    }

    return (count > 0) ? score / count : 0.0f;
}

int DownbeatDetectionService::findDownbeatPhase(const std::vector<float>& accents, int beatsPerBar) {
    int bestPhase = 0;
    float bestScore = -1e10f;

    for (int phase = 0; phase < beatsPerBar; ++phase) {
        float score = scorePhase(accents, phase, beatsPerBar);
        if (score > bestScore) {
            bestScore = score;
            bestPhase = phase;
        }
    }

    return bestPhase;
}

DownbeatResult DownbeatDetectionService::detect(const AudioTrack& track,
                                                  const std::vector<double>& beats, double bpm) {
    spdlog::info("DownbeatDetectionService: analyzing {} beats at {:.1f} BPM", beats.size(), bpm);

    DownbeatResult result;
    result.bpm = bpm;
    result.beatsPerBar = beatsPerBar_;

    if (beats.size() < static_cast<size_t>(beatsPerBar_ * 2)) {
        spdlog::warn("DownbeatDetectionService: not enough beats");
        return result;
    }

    auto accents = computeAccentPattern(track, beats);

    result.downbeatPhase = findDownbeatPhase(accents, beatsPerBar_);

    for (size_t i = result.downbeatPhase; i < beats.size(); i += beatsPerBar_) {
        result.downbeats.push_back(beats[i]);
    }

    float bestScore = scorePhase(accents, result.downbeatPhase, beatsPerBar_);
    float secondBest = -1e10f;
    for (int phase = 0; phase < beatsPerBar_; ++phase) {
        if (phase != result.downbeatPhase) {
            secondBest = std::max(secondBest, scorePhase(accents, phase, beatsPerBar_));
        }
    }

    // Confidence based on how much better the best phase is
    if (secondBest > -1e9f && bestScore > 0) {
        result.confidence = std::clamp((bestScore - secondBest) / bestScore, 0.0f, 1.0f);
    } else {
        result.confidence = 0.5f;
    }

    spdlog::info("DownbeatDetectionService: {} downbeats, phase={}, confidence={:.0f}%",
                 result.downbeats.size(), result.downbeatPhase, result.confidence * 100);
    return result;
}

} // namespace BeatMate::Core
