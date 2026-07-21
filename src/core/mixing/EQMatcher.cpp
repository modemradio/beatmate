#include "EQMatcher.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

std::vector<float> EQMatcher::computeSpectralProfile(const AudioTrack& track) {
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    int fftSize = 4096;
    FFTProcessor fft(fftSize);

    std::vector<float> avgSpectrum(fftSize / 2 + 1, 0.0f);
    int numFrames = 0;

    for (size_t offset = 0; offset + fftSize <= numSamples; offset += fftSize) {
        std::vector<std::complex<float>> spectrum;
        fft.forward(data + offset, spectrum);
        auto mag = fft.getMagnitudes(spectrum);
        for (size_t i = 0; i < mag.size(); ++i) avgSpectrum[i] += mag[i];
        numFrames++;
    }

    if (numFrames > 0) {
        for (auto& v : avgSpectrum) v /= numFrames;
    }

    int lowBin = static_cast<int>(200.0 * fftSize / sr);
    int highBin = static_cast<int>(4000.0 * fftSize / sr);

    float lowEnergy = 0, midEnergy = 0, highEnergy = 0;
    for (int i = 1; i < static_cast<int>(avgSpectrum.size()); ++i) {
        float e = avgSpectrum[i] * avgSpectrum[i];
        if (i <= lowBin) lowEnergy += e;
        else if (i >= highBin) highEnergy += e;
        else midEnergy += e;
    }

    return {std::sqrt(lowEnergy), std::sqrt(midEnergy), std::sqrt(highEnergy)};
}

EQSettings EQMatcher::match(const AudioTrack& trackA, const AudioTrack& trackB) {
    auto profileA = computeSpectralProfile(trackA);
    auto profileB = computeSpectralProfile(trackB);

    EQSettings settings;

    // Objectif : faire sonner B comme A
    auto dbDiff = [](float a, float b) -> float {
        if (b < 1e-10f) return 0.0f;
        return 20.0f * std::log10(a / b);
    };

    settings.lowGain = std::clamp(dbDiff(profileA[0], profileB[0]), -12.0f, 12.0f);
    settings.midGain = std::clamp(dbDiff(profileA[1], profileB[1]), -12.0f, 12.0f);
    settings.highGain = std::clamp(dbDiff(profileA[2], profileB[2]), -12.0f, 12.0f);

    spdlog::info("EQMatcher: low={:.1f}dB mid={:.1f}dB high={:.1f}dB",
                 settings.lowGain, settings.midGain, settings.highGain);
    return settings;
}

} // namespace BeatMate::Core
