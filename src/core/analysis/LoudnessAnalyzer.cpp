#include "LoudnessAnalyzer.h"
#include "../audio/AudioTrack.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

LoudnessAnalyzer::LoudnessAnalyzer() = default;
LoudnessAnalyzer::~LoudnessAnalyzer() = default;

void LoudnessAnalyzer::initKWeighting(int sampleRate) {
    double sr = sampleRate;
    double f0 = 1681.974450955533;
    double Q = 0.7071752369554196;
    double K = std::tan(3.14159265358979 * f0 / sr);
    double Vh = std::pow(10.0, 4.0 / 20.0);
    double Vb = std::pow(Vh, 0.4996667741545416);
    double a0p = 1.0 + K / Q + K * K;

    preFilter_.b0 = (Vh + Vb * K / Q + K * K) / a0p;
    preFilter_.b1 = 2.0 * (K * K - Vh) / a0p;
    preFilter_.b2 = (Vh - Vb * K / Q + K * K) / a0p;
    preFilter_.a1 = 2.0 * (K * K - 1.0) / a0p;
    preFilter_.a2 = (1.0 - K / Q + K * K) / a0p;

    double f0rlb = 38.13547087602444;
    double Qrlb = 0.5003270373238773;
    double Krlb = std::tan(3.14159265358979 * f0rlb / sr);
    double a0rlb = 1.0 + Krlb / Qrlb + Krlb * Krlb;

    rlbFilter_.b0 = 1.0 / a0rlb;
    rlbFilter_.b1 = -2.0 / a0rlb;
    rlbFilter_.b2 = 1.0 / a0rlb;
    rlbFilter_.a1 = 2.0 * (Krlb * Krlb - 1.0) / a0rlb;
    rlbFilter_.a2 = (1.0 - Krlb / Qrlb + Krlb * Krlb) / a0rlb;
}

float LoudnessAnalyzer::computeBlockLoudness(const float* data, int numSamples, int channels) {
    double sumSq = 0.0;

    for (int i = 0; i < numSamples; ++i) {
        for (int ch = 0; ch < channels; ++ch) {
            double s = data[i * channels + ch];
            s = preFilter_.process(s);
            s = rlbFilter_.process(s);
            sumSq += s * s;
        }
    }

    double meanSq = sumSq / (numSamples * channels);
    if (meanSq < 1e-20) return -70.0f;

    return static_cast<float>(-0.691 + 10.0 * std::log10(meanSq));
}

LoudnessResult LoudnessAnalyzer::analyze(const AudioTrack& track) {
    spdlog::info("LoudnessAnalyzer: analyzing {}", track.getFilePath());

    int sr = track.getSampleRate();
    int ch = track.getChannels();
    initKWeighting(sr);

    LoudnessResult result;

    const float* data = track.getRawData();
    size_t totalSamples = track.getTotalSamples() / ch;

    int blockSize400ms = static_cast<int>(0.4 * sr);
    int blockSize3s = static_cast<int>(3.0 * sr);
    int hopSize = static_cast<int>(0.1 * sr);

    std::vector<float> blockLoudness;

    preFilter_.reset();
    rlbFilter_.reset();

    for (size_t pos = 0; pos + blockSize400ms * ch <= track.getTotalSamples(); pos += hopSize * ch) {
        preFilter_.reset();
        rlbFilter_.reset();
        float loudness = computeBlockLoudness(data + pos, blockSize400ms, ch);
        blockLoudness.push_back(loudness);
    }

    if (blockLoudness.empty()) return result;

    result.momentaryLUFS = *std::max_element(blockLoudness.begin(), blockLoudness.end());

    if (totalSamples > static_cast<size_t>(blockSize3s)) {
        float maxShortTerm = -70.0f;
        int stBlocks = blockSize3s / hopSize;
        for (int i = stBlocks; i < static_cast<int>(blockLoudness.size()); ++i) {
            float sum = 0.0f;
            int count = 0;
            for (int j = i - stBlocks; j < i; ++j) {
                if (blockLoudness[j] > -70.0f) {
                    sum += std::pow(10.0f, blockLoudness[j] / 10.0f);
                    count++;
                }
            }
            if (count > 0) {
                float stLoudness = 10.0f * std::log10(sum / count);
                maxShortTerm = std::max(maxShortTerm, stLoudness);
            }
        }
        result.shortTermLUFS = maxShortTerm;
    }

    double sumPower = 0.0;
    int gatedCount = 0;
    for (auto& bl : blockLoudness) {
        if (bl > -70.0f) {
            sumPower += std::pow(10.0, bl / 10.0);
            gatedCount++;
        }
    }

    if (gatedCount > 0) {
        double ungatedLoudness = 10.0 * std::log10(sumPower / gatedCount);
        double relativeThreshold = ungatedLoudness - 10.0;

        sumPower = 0.0;
        gatedCount = 0;
        for (auto& bl : blockLoudness) {
            if (bl > relativeThreshold) {
                sumPower += std::pow(10.0, bl / 10.0);
                gatedCount++;
            }
        }
        if (gatedCount > 0) {
            result.integratedLUFS = static_cast<float>(10.0 * std::log10(sumPower / gatedCount));
        }
    }

    std::vector<float> sortedLoudness;
    for (auto& bl : blockLoudness) {
        if (bl > -70.0f) sortedLoudness.push_back(bl);
    }
    if (sortedLoudness.size() > 10) {
        std::sort(sortedLoudness.begin(), sortedLoudness.end());
        size_t low = static_cast<size_t>(sortedLoudness.size() * 0.1);
        size_t high = static_cast<size_t>(sortedLoudness.size() * 0.95);
        result.loudnessRange = sortedLoudness[high] - sortedLoudness[low];
    }

    float maxPeak = 0.0f;
    size_t total = track.getTotalSamples();
    for (size_t i = 0; i < total; ++i) {
        float abs_s = std::fabs(data[i]);
        if (abs_s > maxPeak) maxPeak = abs_s;
    }
    result.truePeakdBTP = (maxPeak > 0) ? 20.0f * std::log10(maxPeak) : -100.0f;

    spdlog::info("Loudness: integrated={:.1f} LUFS, range={:.1f} LU, truePeak={:.1f} dBTP",
                 result.integratedLUFS, result.loudnessRange, result.truePeakdBTP);
    return result;
}

}
