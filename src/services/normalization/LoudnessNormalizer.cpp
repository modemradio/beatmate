#include "LoudnessNormalizer.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <numeric>

namespace BeatMate::Services::Normalization {


struct BiquadCoeffs {
    double b0, b1, b2, a1, a2;
};

struct BiquadState {
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    double process(double input, const BiquadCoeffs& c) {
        double output = c.b0 * input + c.b1 * x1 + c.b2 * x2
                       - c.a1 * y1 - c.a2 * y2;
        x2 = x1;
        x1 = input;
        y2 = y1;
        y1 = output;
        return output;
    }

    void reset() { x1 = x2 = y1 = y2 = 0; }
};

static BiquadCoeffs getPreFilterCoeffs(int sampleRate) {
    double fs = static_cast<double>(sampleRate);

    double db = 3.999843853973347;
    double f0 = 1681.974450955533;
    double Q = 0.7071752369554196;
    double K = std::tan(3.14159265358979323846 * f0 / fs);

    double Vh = std::pow(10.0, db / 20.0);
    double Vb = std::pow(Vh, 0.4996667741545416);

    double a0_inv = 1.0 / (1.0 + K / Q + K * K);

    BiquadCoeffs c;
    c.b0 = (Vh + Vb * K / Q + K * K) * a0_inv;
    c.b1 = 2.0 * (K * K - Vh) * a0_inv;
    c.b2 = (Vh - Vb * K / Q + K * K) * a0_inv;
    c.a1 = 2.0 * (K * K - 1.0) * a0_inv;
    c.a2 = (1.0 - K / Q + K * K) * a0_inv;

    return c;
}

static BiquadCoeffs getRLBFilterCoeffs(int sampleRate) {
    double fs = static_cast<double>(sampleRate);

    double f0 = 38.13547087602444;
    double Q = 0.5003270373238773;
    double K = std::tan(3.14159265358979323846 * f0 / fs);

    double a0_inv = 1.0 / (1.0 + K / Q + K * K);

    BiquadCoeffs c;
    c.b0 = a0_inv;
    c.b1 = -2.0 * a0_inv;
    c.b2 = a0_inv;
    c.a1 = 2.0 * (K * K - 1.0) * a0_inv;
    c.a2 = (1.0 - K / Q + K * K) * a0_inv;

    return c;
}

Core::AudioTrack LoudnessNormalizer::normalize(const Core::AudioTrack& track, float targetLUFS) {
    float currentLUFS = measureLUFS(track);

    if (currentLUFS <= -100.0f) {
        spdlog::warn("LoudnessNormalizer: Track is silent, cannot normalize");
        return Core::AudioTrack();
    }

    float gainDB = targetLUFS - currentLUFS;

    gainDB = std::max(-20.0f, std::min(gainDB, 20.0f));

    float gainLinear = std::pow(10.0f, gainDB / 20.0f);

    Core::AudioTrack result;
    size_t totalSamples = track.getTotalSamples();
    if (totalSamples > 0) {
        std::vector<float> pcmData(totalSamples);
        for (size_t i = 0; i < totalSamples; ++i) {
            float sample = track.getSample(i) * gainLinear;
            if (sample > 1.0f) sample = 1.0f - std::exp(-(sample - 1.0f));
            else if (sample < -1.0f) sample = -(1.0f - std::exp(-(-sample - 1.0f)));
            pcmData[i] = sample;
        }
        result.loadData(std::move(pcmData), track.getSampleRate(), track.getChannels());
    }

    spdlog::info("LoudnessNormalizer: Normalized from {:.1f} to {:.1f} LUFS (gain: {:.1f} dB)",
                 currentLUFS, targetLUFS, gainDB);
    return result;
}

float LoudnessNormalizer::measureLUFS(const Core::AudioTrack& track) const {
    if (!track.isLoaded()) return -100.0f;

    int sampleRate = track.getSampleRate();
    int channels = track.getChannels();
    size_t totalSamples = track.getTotalSamples();
    size_t numFrames = totalSamples / static_cast<size_t>(channels);

    if (numFrames == 0) return -100.0f;

    auto preCoeffs = getPreFilterCoeffs(sampleRate);
    auto rlbCoeffs = getRLBFilterCoeffs(sampleRate);

    size_t blockSizeSamples = static_cast<size_t>(sampleRate * 0.4); // 400ms blocks
    size_t hopSizeSamples = static_cast<size_t>(sampleRate * 0.1);   // 100ms hop (75% overlap)

    if (blockSizeSamples == 0 || numFrames < blockSizeSamples) {
        blockSizeSamples = numFrames;
        hopSizeSamples = numFrames;
    }

    std::vector<std::vector<double>> filteredChannels(channels);
    for (int ch = 0; ch < channels; ++ch) {
        filteredChannels[ch].resize(numFrames);

        BiquadState preState, rlbState;

        for (size_t frame = 0; frame < numFrames; ++frame) {
            double sample = static_cast<double>(track.getSample(frame * channels + ch));

            double preFiltered = preState.process(sample, preCoeffs);

            double kWeighted = rlbState.process(preFiltered, rlbCoeffs);

            filteredChannels[ch][frame] = kWeighted;
        }
    }


    std::vector<double> blockLoudness;

    for (size_t blockStart = 0; blockStart + blockSizeSamples <= numFrames; blockStart += hopSizeSamples) {
        double totalMeanSquare = 0.0;

        for (int ch = 0; ch < channels; ++ch) {
            double sumSquares = 0.0;
            for (size_t i = blockStart; i < blockStart + blockSizeSamples; ++i) {
                double s = filteredChannels[ch][i];
                sumSquares += s * s;
            }
            double meanSquare = sumSquares / static_cast<double>(blockSizeSamples);

            double weight = 1.0;
            if (channels > 2 && (ch == 3 || ch == 4)) {
                weight = 1.41; // Surround channels Ls, Rs
            }

            totalMeanSquare += weight * meanSquare;
        }

        double blockLUFS = -0.691 + 10.0 * std::log10(totalMeanSquare + 1e-20);
        blockLoudness.push_back(blockLUFS);
    }

    if (blockLoudness.empty()) return -100.0f;


    std::vector<double> passAbsGate;
    for (double bl : blockLoudness) {
        if (bl > -70.0) {
            passAbsGate.push_back(bl);
        }
    }

    if (passAbsGate.empty()) return -100.0f;

    double sumLinear = 0.0;
    for (double bl : passAbsGate) {
        sumLinear += std::pow(10.0, bl / 10.0);
    }
    double meanLoudness = 10.0 * std::log10(sumLinear / static_cast<double>(passAbsGate.size()) + 1e-20);

    double relativeGate = meanLoudness - 10.0;

    std::vector<double> passRelGate;
    for (double bl : passAbsGate) {
        if (bl > relativeGate) {
            passRelGate.push_back(bl);
        }
    }

    if (passRelGate.empty()) return -100.0f;

    double sumFinal = 0.0;
    for (double bl : passRelGate) {
        sumFinal += std::pow(10.0, bl / 10.0);
    }
    double gatedLoudness = 10.0 * std::log10(sumFinal / static_cast<double>(passRelGate.size()) + 1e-20);

    return static_cast<float>(gatedLoudness);
}

} // namespace BeatMate::Services::Normalization
