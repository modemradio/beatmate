#include "EbuR128AnalyzerService.h"
#include "../audio/AudioTrack.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

EbuR128AnalyzerService::EbuR128AnalyzerService() = default;
EbuR128AnalyzerService::~EbuR128AnalyzerService() = default;

void EbuR128AnalyzerService::initKWeighting(int sampleRate) {
    sampleRate_ = sampleRate;
    double fs = static_cast<double>(sampleRate);

    double db = 3.999843853973347;
    double f0 = 1681.974450955533;
    double Q = 0.7071752369554196;
    double K = std::tan(3.14159265358979323846 * f0 / fs);
    double Vh = std::pow(10.0, db / 20.0);
    double Vb = std::pow(Vh, 0.4996667741545416);
    double a0_ = 1.0 + K / Q + K * K;

    for (int ch = 0; ch < 2; ++ch) {
        preFilter_[ch].b0 = (Vh + Vb * K / Q + K * K) / a0_;
        preFilter_[ch].b1 = 2.0 * (K * K - Vh) / a0_;
        preFilter_[ch].b2 = (Vh - Vb * K / Q + K * K) / a0_;
        preFilter_[ch].a1 = 2.0 * (K * K - 1.0) / a0_;
        preFilter_[ch].a2 = (1.0 - K / Q + K * K) / a0_;
        preFilter_[ch].reset();
    }

    double f0_rlb = 38.13547087602444;
    double Q_rlb = 0.5003270373238773;
    double K_rlb = std::tan(3.14159265358979323846 * f0_rlb / fs);
    double a0_rlb = 1.0 + K_rlb / Q_rlb + K_rlb * K_rlb;

    for (int ch = 0; ch < 2; ++ch) {
        rlbFilter_[ch].b0 = 1.0 / a0_rlb;
        rlbFilter_[ch].b1 = -2.0 / a0_rlb;
        rlbFilter_[ch].b2 = 1.0 / a0_rlb;
        rlbFilter_[ch].a1 = 2.0 * (K_rlb * K_rlb - 1.0) / a0_rlb;
        rlbFilter_[ch].a2 = (1.0 - K_rlb / Q_rlb + K_rlb * K_rlb) / a0_rlb;
        rlbFilter_[ch].reset();
    }
}

void EbuR128AnalyzerService::applyKWeighting(const float* input, float* output,
                                               int numSamples, int channel) {
    int ch = std::min(channel, 1);
    for (int i = 0; i < numSamples; ++i) {
        double x = input[i];
        x = preFilter_[ch].process(x);
        x = rlbFilter_[ch].process(x);
        output[i] = static_cast<float>(x);
    }
}

double EbuR128AnalyzerService::computeBlockMeanSquare(const float* data, int numSamples, int channels) {
    double totalMeanSquare = 0.0;

    for (int ch = 0; ch < channels && ch < 2; ++ch) {
        int numFrames = numSamples / channels;
        std::vector<float> channelData(numFrames);
        for (int i = 0; i < numFrames; ++i) {
            channelData[i] = data[i * channels + ch];
        }

        std::vector<float> weighted(numFrames);
        applyKWeighting(channelData.data(), weighted.data(), numFrames, ch);

        double sum = 0.0;
        for (int i = 0; i < numFrames; ++i) {
            sum += weighted[i] * weighted[i];
        }
        totalMeanSquare += sum / numFrames;
    }

    return totalMeanSquare;
}

float EbuR128AnalyzerService::computeGatedLoudness(const std::vector<double>& blockLoudness) {
    if (blockLoudness.empty()) return -70.0f;

    std::vector<double> aboveAbsGate;
    for (double bl : blockLoudness) {
        double lufs = -0.691 + 10.0 * std::log10(std::max(bl, 1e-10));
        if (lufs > -70.0) {
            aboveAbsGate.push_back(bl);
        }
    }

    if (aboveAbsGate.empty()) return -70.0f;

    double avgAbsGate = 0.0;
    for (double v : aboveAbsGate) avgAbsGate += v;
    avgAbsGate /= aboveAbsGate.size();

    double relativeThresholdLUFS = -0.691 + 10.0 * std::log10(std::max(avgAbsGate, 1e-10)) - 10.0;
    double relativeThresholdLinear = std::pow(10.0, (relativeThresholdLUFS + 0.691) / 10.0);

    double gatedSum = 0.0;
    int gatedCount = 0;
    for (double v : aboveAbsGate) {
        if (v >= relativeThresholdLinear) {
            gatedSum += v;
            gatedCount++;
        }
    }

    if (gatedCount == 0) return -70.0f;

    double gatedAvg = gatedSum / gatedCount;
    return static_cast<float>(-0.691 + 10.0 * std::log10(std::max(gatedAvg, 1e-10)));
}

float EbuR128AnalyzerService::computeLRA(const std::vector<double>& shortTermLoudness) {
    if (shortTermLoudness.size() < 2) return 0.0f;

    std::vector<double> lufs;
    for (double st : shortTermLoudness) {
        double l = -0.691 + 10.0 * std::log10(std::max(st, 1e-10));
        if (l > -70.0) lufs.push_back(l);
    }

    if (lufs.size() < 2) return 0.0f;

    double mean = std::accumulate(lufs.begin(), lufs.end(), 0.0) / lufs.size();
    double relThreshold = mean - 20.0;

    std::vector<double> gated;
    for (double l : lufs) {
        if (l > relThreshold) gated.push_back(l);
    }

    if (gated.size() < 2) return 0.0f;

    std::sort(gated.begin(), gated.end());

    size_t p10 = static_cast<size_t>(gated.size() * 0.10);
    size_t p95 = static_cast<size_t>(gated.size() * 0.95);
    p95 = std::min(p95, gated.size() - 1);

    return static_cast<float>(gated[p95] - gated[p10]);
}

float EbuR128AnalyzerService::computeTruePeak(const float* data, size_t numSamples, int channels) {
    float truePeak = 0.0f;

    for (size_t i = 0; i + 1 < numSamples; ++i) {
        float s0 = std::fabs(data[i]);
        float s1 = std::fabs(data[i + 1]);
        truePeak = std::max(truePeak, s0);

        for (int j = 1; j < 4; ++j) {
            float t = j / 4.0f;
            float interp = s0 + (s1 - s0) * t;
            truePeak = std::max(truePeak, interp);
        }
    }

    return 20.0f * std::log10(std::max(truePeak, 1e-10f));
}

float EbuR128AnalyzerService::getMomentaryLoudness(const AudioTrack& track, double timeSeconds) {
    int channels = track.getChannels();
    int sr = track.getSampleRate();
    int blockSamples = static_cast<int>(0.4 * sr) * channels;

    size_t startSample = static_cast<size_t>(timeSeconds * sr) * channels;
    if (startSample + blockSamples > track.getTotalSamples()) return -70.0f;

    initKWeighting(sr);
    const float* data = track.getRawData();
    double ms = computeBlockMeanSquare(data + startSample, blockSamples, channels);
    return static_cast<float>(-0.691 + 10.0 * std::log10(std::max(ms, 1e-10)));
}

float EbuR128AnalyzerService::getShortTermLoudness(const AudioTrack& track, double timeSeconds) {
    int channels = track.getChannels();
    int sr = track.getSampleRate();
    int blockSamples = static_cast<int>(3.0 * sr) * channels;

    size_t startSample = static_cast<size_t>(timeSeconds * sr) * channels;
    if (startSample + blockSamples > track.getTotalSamples()) return -70.0f;

    initKWeighting(sr);
    const float* data = track.getRawData();
    double ms = computeBlockMeanSquare(data + startSample, blockSamples, channels);
    return static_cast<float>(-0.691 + 10.0 * std::log10(std::max(ms, 1e-10)));
}

EbuR128Result EbuR128AnalyzerService::analyze(const AudioTrack& track) {
    spdlog::info("EbuR128AnalyzerService: analyzing {} (ITU-R BS.1770-4)", track.getFilePath());

    EbuR128Result result;
    int channels = track.getChannels();
    int sr = track.getSampleRate();
    const float* data = track.getRawData();
    size_t totalSamples = track.getTotalSamples();

    if (totalSamples == 0) return result;

    initKWeighting(sr);

    int momentaryBlockFrames = static_cast<int>(0.4 * sr);
    int momentaryBlockSamples = momentaryBlockFrames * channels;
    int momentaryHop = momentaryBlockFrames / 4;

    int shortTermBlockFrames = static_cast<int>(3.0 * sr);
    int shortTermBlockSamples = shortTermBlockFrames * channels;
    int shortTermHop = shortTermBlockFrames / 3;

    std::vector<double> momentaryBlocks;
    int numMomentaryBlocks = static_cast<int>((totalSamples / channels - momentaryBlockFrames) / momentaryHop) + 1;

    for (int i = 0; i < numMomentaryBlocks; ++i) {
        size_t offset = static_cast<size_t>(i) * momentaryHop * channels;
        if (offset + momentaryBlockSamples > totalSamples) break;

        for (int ch = 0; ch < 2; ++ch) {
            preFilter_[ch].reset();
            rlbFilter_[ch].reset();
        }

        double ms = computeBlockMeanSquare(data + offset, momentaryBlockSamples, channels);
        momentaryBlocks.push_back(ms);

        float lufs = static_cast<float>(-0.691 + 10.0 * std::log10(std::max(ms, 1e-10)));
        result.momentaryCurve.push_back(lufs);
        result.maxMomentaryLUFS = std::max(result.maxMomentaryLUFS, lufs);
    }

    result.momentaryInterval = static_cast<float>(momentaryHop) / sr;

    std::vector<double> shortTermBlocks;
    int numSTBlocks = static_cast<int>((totalSamples / channels - shortTermBlockFrames) / shortTermHop) + 1;

    for (int i = 0; i < numSTBlocks; ++i) {
        size_t offset = static_cast<size_t>(i) * shortTermHop * channels;
        if (offset + shortTermBlockSamples > totalSamples) break;

        for (int ch = 0; ch < 2; ++ch) {
            preFilter_[ch].reset();
            rlbFilter_[ch].reset();
        }

        double ms = computeBlockMeanSquare(data + offset, shortTermBlockSamples, channels);
        shortTermBlocks.push_back(ms);

        float lufs = static_cast<float>(-0.691 + 10.0 * std::log10(std::max(ms, 1e-10)));
        result.shortTermCurve.push_back(lufs);
        result.maxShortTermLUFS = std::max(result.maxShortTermLUFS, lufs);
    }

    result.integratedLUFS = computeGatedLoudness(momentaryBlocks);

    if (!result.momentaryCurve.empty()) {
        result.momentaryLUFS = *std::max_element(result.momentaryCurve.begin(), result.momentaryCurve.end());
    }
    if (!result.shortTermCurve.empty()) {
        result.shortTermLUFS = *std::max_element(result.shortTermCurve.begin(), result.shortTermCurve.end());
    }

    result.loudnessRange = computeLRA(shortTermBlocks);

    result.truePeakdBTP = computeTruePeak(data, totalSamples, channels);

    result.absoluteGateThreshold = -70.0f;
    result.relativeGateThreshold = result.integratedLUFS - 10.0f;

    spdlog::info("EbuR128AnalyzerService: integrated={:.1f} LUFS, range={:.1f} LU, truePeak={:.1f} dBTP",
                 result.integratedLUFS, result.loudnessRange, result.truePeakdBTP);
    return result;
}

}
