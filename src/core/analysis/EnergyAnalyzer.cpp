#include "EnergyAnalyzer.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

EnergyAnalyzer::EnergyAnalyzer() = default;
EnergyAnalyzer::~EnergyAnalyzer() = default;

float EnergyAnalyzer::computeRMS(const float* data, size_t numSamples) const {
    if (numSamples == 0) return 0.0f;
    double sum = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        sum += data[i] * data[i];
    }
    return static_cast<float>(std::sqrt(sum / numSamples));
}

float EnergyAnalyzer::computeSpectralCentroid(const float* data, size_t numSamples, int sampleRate) const {
    int fftSize = 2048;
    if (static_cast<int>(numSamples) < fftSize) return 0.0f;

    FFTProcessor fft(fftSize);
    std::vector<std::complex<float>> spectrum;
    fft.forward(data, spectrum);
    auto mag = fft.getMagnitudes(spectrum);

    double weightedSum = 0.0, magSum = 0.0;
    for (int i = 1; i < static_cast<int>(mag.size()); ++i) {
        double freq = static_cast<double>(i) * sampleRate / fftSize;
        weightedSum += freq * mag[i];
        magSum += mag[i];
    }

    return (magSum > 0) ? static_cast<float>(weightedSum / magSum) : 0.0f;
}

EnergyResult EnergyAnalyzer::analyze(const AudioTrack& track, double segmentDuration) {
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t totalSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    EnergyResult result;
    result.segmentDuration = segmentDuration;

    size_t segmentSamples = static_cast<size_t>(segmentDuration * sr);
    size_t numSegments = totalSamples / segmentSamples;

    float totalCentroid = 0.0f;

    for (size_t seg = 0; seg < numSegments; ++seg) {
        const float* segData = data + seg * segmentSamples;
        float rms = computeRMS(segData, segmentSamples);
        float centroid = computeSpectralCentroid(segData, segmentSamples, sr);

        result.curve.push_back(rms);
        totalCentroid += centroid;
    }

    const float avgCentroid = result.curve.empty() ? 0.0f
        : totalCentroid / static_cast<float>(result.curve.size());
    result = fromRmsCurve(std::move(result.curve), segmentDuration, avgCentroid);

    spdlog::info("EnergyAnalyzer: overall={}/10, rms={:.4f}, centroid={:.0f}Hz",
                 result.overall, result.rmsAverage, result.spectralCentroid);
    return result;
}

EnergyResult EnergyAnalyzer::fromRmsCurve(std::vector<float> rmsPerSegment,
                                          double segmentDuration,
                                          float spectralCentroid) {
    EnergyResult result;
    result.segmentDuration = segmentDuration;
    result.spectralCentroid = spectralCentroid;
    result.curve = std::move(rmsPerSegment);
    if (result.curve.empty()) return result;

    result.rmsAverage = std::accumulate(result.curve.begin(), result.curve.end(), 0.0f)
                      / static_cast<float>(result.curve.size());

    float maxRMS = *std::max_element(result.curve.begin(), result.curve.end());
    if (maxRMS > 0) {
        for (auto& v : result.curve) v /= maxRMS;
    }

    float rmsDb = 20.0f * std::log10(std::max(result.rmsAverage, 1e-6f));
    // Typical range: -40 to -6 dB
    float normalized = (rmsDb + 40.0f) / 34.0f;
    result.overall = static_cast<int>(std::clamp(normalized * 10.0f, 1.0f, 10.0f));
    return result;
}

std::vector<EnergySection> EnergyAnalyzer::sectionize(const EnergyResult& result,
                                                      double minSectionSec) {
    std::vector<EnergySection> sections;
    const auto& curve = result.curve;
    const double dt = result.segmentDuration;
    if (curve.empty() || dt <= 0.0) return sections;

    const int half = std::max(1, static_cast<int>(std::lround(2.0 / dt)));
    std::vector<int> quantized(curve.size());
    for (size_t i = 0; i < curve.size(); ++i) {
        const size_t lo = i >= static_cast<size_t>(half) ? i - half : 0;
        const size_t hi = std::min(curve.size() - 1, i + static_cast<size_t>(half));
        double sum = 0.0;
        for (size_t j = lo; j <= hi; ++j) sum += curve[j];
        const double smoothed = sum / static_cast<double>(hi - lo + 1);
        quantized[i] = std::clamp(static_cast<int>(std::lround(smoothed * 10.0)), 1, 10);
    }

    size_t runStart = 0;
    for (size_t i = 1; i <= quantized.size(); ++i) {
        if (i == quantized.size() || quantized[i] != quantized[runStart]) {
            sections.push_back({ runStart * dt, i * dt, quantized[runStart] });
            runStart = i;
        }
    }

    bool merged = true;
    while (merged && sections.size() > 1) {
        merged = false;
        for (size_t i = 0; i < sections.size(); ++i) {
            if (sections[i].endSec - sections[i].startSec >= minSectionSec) continue;
            size_t neighbor;
            if (i == 0) neighbor = 1;
            else if (i == sections.size() - 1) neighbor = i - 1;
            else neighbor = std::abs(sections[i - 1].energy - sections[i].energy)
                          <= std::abs(sections[i + 1].energy - sections[i].energy)
                          ? i - 1 : i + 1;
            auto& keep = sections[std::min(i, neighbor)];
            const auto& gone = sections[std::max(i, neighbor)];
            const double keepLen = keep.endSec - keep.startSec;
            const double goneLen = gone.endSec - gone.startSec;
            const double total = keepLen + goneLen;
            keep.energy = total > 0.0
                ? std::clamp(static_cast<int>(std::lround(
                      (keep.energy * keepLen + gone.energy * goneLen) / total)), 1, 10)
                : keep.energy;
            keep.endSec = gone.endSec;
            sections.erase(sections.begin() + static_cast<long long>(std::max(i, neighbor)));
            merged = true;
            break;
        }
    }

    return sections;
}

} // namespace BeatMate::Core
