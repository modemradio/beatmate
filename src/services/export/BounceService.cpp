#include "BounceService.h"
#include <spdlog/spdlog.h>
#define _USE_MATH_DEFINES
#include <cmath>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace BeatMate::Services::Export {

BounceService::BounceService() = default;

BounceResult BounceService::bounceTrack(const Core::AudioTrack& audio, const BounceOptions& options,
                                          BounceProgressCallback progress) {
    BounceResult result;
    auto startTime = std::chrono::steady_clock::now();

    if (!audio.isLoaded()) {
        result.errorMessage = "Audio track not loaded";
        return result;
    }

    isBouncing_ = true;
    cancelRequested_ = false;

    if (progress) progress(0.0f, "Preparing bounce...");

    size_t totalSamples = audio.getTotalSamples();
    int channels = audio.getChannels();
    int sampleRate = audio.getSampleRate();

    size_t startSample = static_cast<size_t>(options.startTimeSeconds * sampleRate * channels);
    size_t endSample = (options.endTimeSeconds > 0.0)
                           ? static_cast<size_t>(options.endTimeSeconds * sampleRate * channels)
                           : totalSamples;
    startSample = std::min(startSample, totalSamples);
    endSample = std::min(endSample, totalSamples);

    std::vector<float> samples(endSample - startSample);
    for (size_t i = 0; i < samples.size(); ++i) {
        if (cancelRequested_) {
            result.errorMessage = "Bounce cancelled";
            isBouncing_ = false;
            return result;
        }
        samples[i] = audio.getSample(startSample + i);
    }

    if (progress) progress(0.4f, "Processing...");

    if (options.normalizeOutput) {
        float currentLUFS = measureLUFS(samples, sampleRate, channels);
        float gainDb = options.targetLUFS - currentLUFS;
        float gainLinear = std::pow(10.0f, gainDb / 20.0f);
        for (auto& s : samples) s *= gainLinear;
        result.integratedLUFS = options.targetLUFS;
    } else {
        result.integratedLUFS = measureLUFS(samples, sampleRate, channels);
    }

    result.peakLevel = measurePeakLevel(samples);
    result.rmsLevel = measureRMSLevel(samples);

    if (progress) progress(0.6f, "Encoding...");

    std::string outputPath;
    if (!options.outputDirectory.empty()) {
        std::filesystem::create_directories(options.outputDirectory);
        std::string prefix = options.fileNamePrefix.empty() ? "bounce" : options.fileNamePrefix;
        outputPath = options.outputDirectory + "/" + prefix + "." +
                     MultiFormatExporter::formatExtension(options.format);
    } else {
        outputPath = "bounce." + MultiFormatExporter::formatExtension(options.format);
    }

    Core::AudioTrack exportTrack;
    exportTrack.loadData(std::move(samples), sampleRate, channels);

    auto exportResult = exporter_.exportAudioTrack(exportTrack, options.format,
                                                     options.exportOptions, outputPath);

    if (progress) progress(1.0f, "Complete");

    auto endClock = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(endClock - startTime).count();

    result.success = exportResult.success;
    result.outputPath = exportResult.outputPath;
    result.errorMessage = exportResult.errorMessage;
    result.durationSeconds = exportResult.durationSeconds;
    result.fileSizeBytes = exportResult.fileSizeBytes;
    result.processingTimeSeconds = elapsed;

    isBouncing_ = false;
    spdlog::info("BounceService: Bounce complete ({:.1f}s processing, {:.1f}s audio, {} bytes)",
                 elapsed, result.durationSeconds, result.fileSizeBytes);
    return result;
}

BounceResult BounceService::bounceMix(const std::vector<Core::AudioTrack*>& stems,
                                        const std::vector<float>& volumes,
                                        const std::vector<float>& pans,
                                        const BounceOptions& options,
                                        BounceProgressCallback progress) {
    BounceResult result;
    if (stems.empty()) {
        result.errorMessage = "No stems provided";
        return result;
    }

    isBouncing_ = true;
    if (progress) progress(0.0f, "Mixing stems...");

    int sampleRate = stems[0]->getSampleRate();
    int channels = 2;

    size_t maxFrames = 0;
    for (const auto* stem : stems) {
        maxFrames = std::max(maxFrames, stem->getNumFrames());
    }

    std::vector<float> mixedOutput;
    mixSamples(stems, volumes, pans, mixedOutput, sampleRate, channels);

    if (progress) progress(0.5f, "Processing mix...");

    Core::AudioTrack mixTrack;
    mixTrack.loadData(std::move(mixedOutput), sampleRate, channels);

    result = bounceTrack(mixTrack, options, [&progress](float p, const std::string& s) {
        if (progress) progress(0.5f + p * 0.5f, s);
    });

    isBouncing_ = false;
    return result;
}

BounceResult BounceService::bounceWithCrossfade(const Core::AudioTrack& trackA,
                                                   const Core::AudioTrack& trackB,
                                                   double crossfadeMs,
                                                   const BounceOptions& options) {
    BounceResult result;
    if (!trackA.isLoaded() || !trackB.isLoaded()) {
        result.errorMessage = "Tracks not loaded";
        return result;
    }

    int sampleRate = trackA.getSampleRate();
    int channels = trackA.getChannels();
    size_t crossfadeSamples = static_cast<size_t>(crossfadeMs / 1000.0 * sampleRate * channels);

    size_t totalA = trackA.getTotalSamples();
    size_t totalB = trackB.getTotalSamples();

    if (crossfadeSamples > totalA) crossfadeSamples = totalA;
    if (crossfadeSamples > totalB) crossfadeSamples = totalB;

    size_t overlapStart = totalA - crossfadeSamples;
    size_t outputSize = overlapStart + totalB;

    std::vector<float> output(outputSize, 0.0f);

    for (size_t i = 0; i < overlapStart; ++i) {
        output[i] = trackA.getSample(i);
    }

    for (size_t i = 0; i < crossfadeSamples && i < totalB; ++i) {
        float fadeOut = 1.0f - static_cast<float>(i) / crossfadeSamples;
        float fadeIn = static_cast<float>(i) / crossfadeSamples;
        fadeOut = std::cos(fadeIn * 3.14159265f * 0.5f);
        fadeIn = std::sin(fadeIn * 3.14159265f * 0.5f);

        float sampleA = (overlapStart + i < totalA) ? trackA.getSample(overlapStart + i) : 0.0f;
        float sampleB = trackB.getSample(i);
        output[overlapStart + i] = sampleA * fadeOut + sampleB * fadeIn;
    }

    for (size_t i = crossfadeSamples; i < totalB; ++i) {
        output[overlapStart + i] = trackB.getSample(i);
    }

    Core::AudioTrack mixTrack;
    mixTrack.loadData(std::move(output), sampleRate, channels);
    return bounceTrack(mixTrack, options);
}

std::vector<BounceResult> BounceService::bounceSplit(const Core::AudioTrack& audio,
                                                       const std::vector<double>& splitPointsSeconds,
                                                       const BounceOptions& options) {
    std::vector<BounceResult> results;
    std::vector<double> points = splitPointsSeconds;
    points.insert(points.begin(), 0.0);
    points.push_back(audio.getDuration());

    for (size_t i = 0; i + 1 < points.size(); ++i) {
        BounceOptions segmentOptions = options;
        segmentOptions.startTimeSeconds = points[i];
        segmentOptions.endTimeSeconds = points[i + 1];

        std::string prefix = options.fileNamePrefix.empty() ? "segment" : options.fileNamePrefix;
        segmentOptions.fileNamePrefix = prefix + "_" + std::to_string(i + 1);

        results.push_back(bounceTrack(audio, segmentOptions));
    }
    return results;
}

bool BounceService::cancelBounce() {
    if (isBouncing_) {
        cancelRequested_ = true;
        spdlog::info("BounceService: Cancel requested");
        return true;
    }
    return false;
}


float BounceService::measurePeakLevel(const std::vector<float>& samples) const {
    float peak = 0.0f;
    for (auto s : samples) peak = std::max(peak, std::abs(s));
    return 20.0f * std::log10(peak + 1e-10f);
}

float BounceService::measureRMSLevel(const std::vector<float>& samples) const {
    if (samples.empty()) return -100.0f;
    double sum = 0.0;
    for (auto s : samples) sum += s * s;
    float rms = std::sqrt(static_cast<float>(sum / samples.size()));
    return 20.0f * std::log10(rms + 1e-10f);
}

namespace {
struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;
    inline double process(double x) {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0.0; }
};

static Biquad makeHighShelf(double fs) {
    const double f0 = 1681.974450955533;
    const double G  = 3.999843853973347;
    const double Q  = 0.7071752369554196;
    const double K  = std::tan(M_PI * f0 / fs);
    const double Vh = std::pow(10.0, G / 20.0);
    const double Vb = std::pow(Vh, 0.4996667741545416);
    const double a0 = 1.0 + K / Q + K * K;
    Biquad b;
    b.b0 = (Vh + Vb * K / Q + K * K) / a0;
    b.b1 = 2.0 * (K * K - Vh) / a0;
    b.b2 = (Vh - Vb * K / Q + K * K) / a0;
    b.a1 = 2.0 * (K * K - 1.0) / a0;
    b.a2 = (1.0 - K / Q + K * K) / a0;
    return b;
}

static Biquad makeHighPass(double fs) {
    const double f0 = 38.13547087602444;
    const double Q  = 0.5003270373238773;
    const double K  = std::tan(M_PI * f0 / fs);
    const double a0 = 1.0 + K / Q + K * K;
    Biquad b;
    b.b0 = 1.0 / a0;
    b.b1 = -2.0 / a0;
    b.b2 = 1.0 / a0;
    b.a1 = 2.0 * (K * K - 1.0) / a0;
    b.a2 = (1.0 - K / Q + K * K) / a0;
    return b;
}

static double channelWeight(int chIndex, int totalChannels) {
    if (totalChannels <= 2) return 1.0;
    switch (totalChannels) {
        case 3:
            return 1.0;
        case 4:
            return (chIndex < 2) ? 1.0 : 1.41;
        case 5:
            if (chIndex < 3) return 1.0;
            return 1.41;
        case 6:
            if (chIndex == 3) return 0.0;
            if (chIndex < 3)  return 1.0;
            return 1.41;
        default:
            return 1.0;
    }
}
}

float BounceService::measureLUFS(const std::vector<float>& samples, int sampleRate, int channels) const {
    if (samples.empty() || channels <= 0) return -100.0f;
    const size_t numFrames = samples.size() / static_cast<size_t>(channels);
    if (numFrames == 0) return -100.0f;

    std::vector<Biquad> shelf(channels), hp(channels);
    for (int ch = 0; ch < channels; ++ch) {
        shelf[ch] = makeHighShelf(static_cast<double>(sampleRate));
        hp[ch]    = makeHighPass(static_cast<double>(sampleRate));
    }

    std::vector<std::vector<double>> kw(channels, std::vector<double>(numFrames));
    for (size_t f = 0; f < numFrames; ++f) {
        for (int ch = 0; ch < channels; ++ch) {
            double x = static_cast<double>(samples[f * channels + ch]);
            double y = shelf[ch].process(x);
            y = hp[ch].process(y);
            kw[ch][f] = y;
        }
    }

    const size_t blockSize = static_cast<size_t>(0.400 * sampleRate);
    const size_t hopSize   = static_cast<size_t>(0.100 * sampleRate);
    if (numFrames < blockSize) {
        double ms = 0.0;
        for (int ch = 0; ch < channels; ++ch) {
            double w = channelWeight(ch, channels);
            if (w == 0.0) continue;
            double s = 0.0;
            for (size_t f = 0; f < numFrames; ++f) s += kw[ch][f] * kw[ch][f];
            ms += w * (s / numFrames);
        }
        return static_cast<float>(-0.691 + 10.0 * std::log10(ms + 1e-20));
    }

    std::vector<double> blockLoudness;
    blockLoudness.reserve((numFrames - blockSize) / hopSize + 1);
    for (size_t start = 0; start + blockSize <= numFrames; start += hopSize) {
        double weightedMS = 0.0;
        for (int ch = 0; ch < channels; ++ch) {
            double w = channelWeight(ch, channels);
            if (w == 0.0) continue;
            double s = 0.0;
            for (size_t i = 0; i < blockSize; ++i) {
                double y = kw[ch][start + i];
                s += y * y;
            }
            weightedMS += w * (s / blockSize);
        }
        blockLoudness.push_back(-0.691 + 10.0 * std::log10(weightedMS + 1e-20));
    }
    if (blockLoudness.empty()) return -100.0f;

    auto integrate = [](const std::vector<double>& blocks, double threshold) {
        double sumLin = 0.0;
        size_t n = 0;
        for (double l : blocks) {
            if (l > threshold) {
                sumLin += std::pow(10.0, (l + 0.691) / 10.0);
                ++n;
            }
        }
        if (n == 0) return -std::numeric_limits<double>::infinity();
        return -0.691 + 10.0 * std::log10(sumLin / n + 1e-20);
    };

    double ungated = integrate(blockLoudness, -70.0);
    if (!std::isfinite(ungated)) return -100.0f;
    double relativeThreshold = ungated - 10.0;
    double gated = integrate(blockLoudness, relativeThreshold);
    if (!std::isfinite(gated)) gated = ungated;
    return static_cast<float>(gated);
}


void BounceService::mixSamples(const std::vector<Core::AudioTrack*>& stems,
                                 const std::vector<float>& volumes,
                                 const std::vector<float>& pans,
                                 std::vector<float>& output,
                                 int sampleRate, int channels) {
    size_t maxFrames = 0;
    for (const auto* stem : stems) {
        maxFrames = std::max(maxFrames, stem->getNumFrames());
    }

    output.resize(maxFrames * channels, 0.0f);

    for (size_t s = 0; s < stems.size(); ++s) {
        float vol = (s < volumes.size()) ? volumes[s] : 1.0f;
        float pan = (s < pans.size()) ? pans[s] : 0.0f;

        float leftGain = vol * std::cos((pan + 1.0f) * 0.25f * 3.14159265f);
        float rightGain = vol * std::sin((pan + 1.0f) * 0.25f * 3.14159265f);

        size_t stemFrames = stems[s]->getNumFrames();
        int stemChannels = stems[s]->getChannels();

        for (size_t f = 0; f < stemFrames; ++f) {
            float left = stems[s]->getSample(f, 0);
            float right = (stemChannels > 1) ? stems[s]->getSample(f, 1) : left;

            if (channels >= 1) output[f * channels + 0] += left * leftGain;
            if (channels >= 2) output[f * channels + 1] += right * rightGain;
        }
    }

    constexpr float kDrive = 0.85f;
    const float kInvDrive = 1.0f / std::tanh(kDrive);
    for (auto& s : output) {
        s = std::tanh(s * kDrive) * kInvDrive;
        if (s > 1.0f) s = 1.0f;
        else if (s < -1.0f) s = -1.0f;
    }
}

}
