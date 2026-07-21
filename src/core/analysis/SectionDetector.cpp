#include "SectionDetector.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

SectionDetector::SectionDetector() = default;
SectionDetector::~SectionDetector() = default;

float SectionDetector::computeSpectralDifference(const std::vector<float>& specA,
                                                   const std::vector<float>& specB) {
    float diff = 0.0f;
    size_t n = std::min(specA.size(), specB.size());
    for (size_t i = 0; i < n; ++i) {
        float d = specA[i] - specB[i];
        if (d > 0) diff += d;
    }
    return diff;
}

std::vector<double> SectionDetector::detectChanges(const AudioTrack& track) {
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    int fftSize = 2048;
    int hopSize = 1024;
    FFTProcessor fft(fftSize);

    std::vector<float> prevMag;
    std::vector<float> spectralFlux;
    double hopDuration = static_cast<double>(hopSize) / sr;

    for (size_t offset = 0; offset + fftSize <= numSamples; offset += hopSize) {
        std::vector<std::complex<float>> spectrum;
        fft.forward(data + offset, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        if (!prevMag.empty()) {
            float flux = computeSpectralDifference(mag, prevMag);
            spectralFlux.push_back(flux);
        }
        prevMag = mag;
    }

    float mean = 0;
    for (auto& v : spectralFlux) mean += v;
    if (!spectralFlux.empty()) mean /= spectralFlux.size();

    float stddev = 0;
    for (auto& v : spectralFlux) stddev += (v - mean) * (v - mean);
    if (!spectralFlux.empty()) stddev = std::sqrt(stddev / spectralFlux.size());

    float threshold = mean + stddev * 1.5f;
    int minGap = static_cast<int>(2.0 / hopDuration); // 2 seconds minimum

    std::vector<double> changes;
    int lastChange = -minGap;

    for (int i = 1; i < static_cast<int>(spectralFlux.size()) - 1; ++i) {
        if (spectralFlux[i] > threshold &&
            spectralFlux[i] > spectralFlux[i-1] &&
            spectralFlux[i] > spectralFlux[i+1] &&
            (i - lastChange) >= minGap) {
            changes.push_back(i * hopDuration);
            lastChange = i;
        }
    }

    return changes;
}

std::vector<Section> SectionDetector::detect(const AudioTrack& track, double bpm) {
    spdlog::info("SectionDetector: fine-grained analysis");

    auto changes = detectChanges(track);
    double duration = track.getDuration();

    std::vector<Section> sections;
    double prevTime = 0.0;

    for (auto& changeTime : changes) {
        Section sec;
        sec.startTime = prevTime;
        sec.endTime = changeTime;
        sec.type = SectionType::Unknown;
        sec.label = "Section";
        sec.confidence = 0.6f;
        sections.push_back(sec);
        prevTime = changeTime;
    }

    if (prevTime < duration) {
        Section sec;
        sec.startTime = prevTime;
        sec.endTime = duration;
        sec.type = SectionType::Unknown;
        sec.label = "Section";
        sec.confidence = 0.6f;
        sections.push_back(sec);
    }

    spdlog::info("SectionDetector: found {} sections", sections.size());
    return sections;
}

} // namespace BeatMate::Core
