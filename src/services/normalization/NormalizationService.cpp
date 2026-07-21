#include "NormalizationService.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace BeatMate::Services::Normalization {

NormalizationResult NormalizationService::normalize(Core::AudioTrack& track,
                                                      const NormalizationOptions& options,
                                                      NormalizationProgressCallback progress) {
    NormalizationResult result;
    if (!track.isLoaded()) {
        spdlog::error("NormalizationService: Track not loaded");
        return result;
    }

    if (progress) progress(0.0f, "Analyzing...");

    size_t totalSamples = track.getTotalSamples();
    int channels = track.getChannels();
    int sampleRate = track.getSampleRate();

    std::vector<float> samples(totalSamples);
    for (size_t i = 0; i < totalSamples; ++i) {
        samples[i] = track.getSample(i);
    }

    if (options.applyDCOffset) {
        removeDCOffset(samples, channels);
    }

    if (progress) progress(0.2f, "Measuring loudness...");

    result.originalPeak = 0.0f;
    for (auto s : samples) result.originalPeak = std::max(result.originalPeak, std::abs(s));
    result.originalPeak = 20.0f * std::log10(result.originalPeak + 1e-10f);

    double sumSq = 0.0;
    for (auto s : samples) sumSq += s * s;
    result.originalRMS = 20.0f * std::log10(std::sqrt(sumSq / totalSamples) + 1e-10f);

    // LUFS (ITU-R BS.1770 simplified)
    result.originalLUFS = measureIntegratedLUFS(track);

    if (progress) progress(0.4f, "Computing gain...");

    float gainDb = 0.0f;
    switch (options.mode) {
        case NormalizationMode::LUFS:
            gainDb = options.targetLUFS - result.originalLUFS;
            break;
        case NormalizationMode::Peak:
            gainDb = options.targetPeakDb - result.originalPeak;
            break;
        case NormalizationMode::RMS:
            gainDb = options.targetRMSDb - result.originalRMS;
            break;
        case NormalizationMode::ReplayGain:
            gainDb = -18.0f - result.originalLUFS; // Reference level -18 LUFS
            break;
    }

    result.appliedGainDb = gainDb;
    float gainLinear = std::pow(10.0f, gainDb / 20.0f);

    if (progress) progress(0.6f, "Applying gain...");

    applyGain(samples, gainLinear);

    if (options.useLimiter) {
        if (progress) progress(0.7f, "Limiting...");
        applyLimiter(samples, options.limiterThreshold, options.limiterRelease,
                      sampleRate, result.limiterEngaged);
    }

    if (options.maxTruePeakDb < 0.0f) {
        float peakAfter = 0.0f;
        for (auto s : samples) peakAfter = std::max(peakAfter, std::abs(s));
        float peakDb = 20.0f * std::log10(peakAfter + 1e-10f);
        if (peakDb > options.maxTruePeakDb) {
            float reduction = std::pow(10.0f, (options.maxTruePeakDb - peakDb) / 20.0f);
            for (auto& s : samples) s *= reduction;
        }
    }

    if (progress) progress(0.85f, "Finalizing...");

    result.resultingPeak = 0.0f;
    for (auto s : samples) result.resultingPeak = std::max(result.resultingPeak, std::abs(s));
    result.resultingTruePeak = 20.0f * std::log10(result.resultingPeak + 1e-10f);
    result.resultingPeak = result.resultingTruePeak;

    track.loadData(std::move(samples), sampleRate, channels);

    result.resultingLUFS = measureIntegratedLUFS(track);
    result.success = true;

    if (progress) progress(1.0f, "Complete");

    spdlog::info("NormalizationService: Normalized from {:.1f} to {:.1f} LUFS (gain: {:.1f} dB, peak: {:.1f} dBFS)",
                 result.originalLUFS, result.resultingLUFS, result.appliedGainDb, result.resultingTruePeak);
    return result;
}

NormalizationResult NormalizationService::normalizeWithPreset(Core::AudioTrack& track,
                                                                NormalizationPreset preset,
                                                                NormalizationProgressCallback progress) {
    auto options = getPresetOptions(preset);
    spdlog::info("NormalizationService: Using preset '{}' (target: {:.1f} LUFS)",
                 presetName(preset), options.targetLUFS);
    return normalize(track, options, progress);
}

std::vector<NormalizationResult> NormalizationService::normalizeBatch(
    std::vector<Core::AudioTrack*>& tracks,
    const NormalizationOptions& options,
    NormalizationProgressCallback progress) {

    std::vector<NormalizationResult> results;
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (progress) {
            float base = static_cast<float>(i) / tracks.size();
            progress(base, "Normalizing track " + std::to_string(i + 1) + "/" + std::to_string(tracks.size()));
        }
        results.push_back(normalize(*tracks[i], options));
    }
    return results;
}


NormalizationResult NormalizationService::analyzeTrack(const Core::AudioTrack& track) const {
    NormalizationResult result;
    if (!track.isLoaded()) return result;

    size_t total = track.getTotalSamples();
    float peak = 0.0f;
    double sumSq = 0.0;
    for (size_t i = 0; i < total; ++i) {
        float s = track.getSample(i);
        peak = std::max(peak, std::abs(s));
        sumSq += s * s;
    }

    result.originalPeak = 20.0f * std::log10(peak + 1e-10f);
    result.originalRMS = 20.0f * std::log10(std::sqrt(sumSq / total) + 1e-10f);
    result.originalLUFS = measureIntegratedLUFS(track);
    result.loudnessRange = measureLoudnessRange(track);
    result.success = true;
    return result;
}

float NormalizationService::measureIntegratedLUFS(const Core::AudioTrack& track) const {
    if (!track.isLoaded()) return -100.0f;

    int channels = track.getChannels();
    size_t numFrames = track.getNumFrames();
    int sampleRate = track.getSampleRate();

    // ITU-R BS.1770 gate at 400ms blocks
    size_t blockSize = static_cast<size_t>(sampleRate * 0.4) * channels;
    if (blockSize == 0 || numFrames == 0) return -100.0f;

    std::vector<double> blockLoudness;
    size_t totalSamples = track.getTotalSamples();

    for (size_t offset = 0; offset + blockSize <= totalSamples; offset += blockSize / 4) {
        double sumSq = 0.0;
        size_t end = std::min(offset + blockSize, totalSamples);
        for (size_t i = offset; i < end; ++i) {
            float s = track.getSample(i);
            sumSq += s * s;
        }
        double meanSq = sumSq / (end - offset);
        double blockLufs = 10.0 * std::log10(meanSq + 1e-20) - 0.691;
        blockLoudness.push_back(blockLufs);
    }

    if (blockLoudness.empty()) return -100.0f;

    // Absolute gate: -70 LUFS
    double sumAbove = 0.0;
    int countAbove = 0;
    for (auto bl : blockLoudness) {
        if (bl > -70.0) { sumAbove += std::pow(10.0, bl / 10.0); countAbove++; }
    }
    if (countAbove == 0) return -100.0f;

    double absGateLevel = 10.0 * std::log10(sumAbove / countAbove);
    double relGate = absGateLevel - 10.0; // Relative gate: -10 LU below absolute-gated level

    double sumFinal = 0.0;
    int countFinal = 0;
    for (auto bl : blockLoudness) {
        if (bl > relGate) { sumFinal += std::pow(10.0, bl / 10.0); countFinal++; }
    }

    if (countFinal == 0) return -100.0f;
    return static_cast<float>(10.0 * std::log10(sumFinal / countFinal));
}

float NormalizationService::measureShortTermLUFS(const Core::AudioTrack& track, double positionSeconds) const {
    if (!track.isLoaded()) return -100.0f;
    int sampleRate = track.getSampleRate();
    int channels = track.getChannels();
    size_t startFrame = static_cast<size_t>(positionSeconds * sampleRate);
    size_t windowFrames = static_cast<size_t>(3.0 * sampleRate);

    double sumSq = 0.0;
    size_t count = 0;
    for (size_t f = startFrame; f < startFrame + windowFrames && f < track.getNumFrames(); ++f) {
        for (int ch = 0; ch < channels; ++ch) {
            float s = track.getSample(f, ch);
            sumSq += s * s;
            count++;
        }
    }
    if (count == 0) return -100.0f;
    return static_cast<float>(10.0 * std::log10(sumSq / count + 1e-20) - 0.691);
}

float NormalizationService::measureMomentaryLUFS(const Core::AudioTrack& track, double positionSeconds) const {
    if (!track.isLoaded()) return -100.0f;
    int sampleRate = track.getSampleRate();
    int channels = track.getChannels();
    size_t startFrame = static_cast<size_t>(positionSeconds * sampleRate);
    size_t windowFrames = static_cast<size_t>(0.4 * sampleRate);

    double sumSq = 0.0;
    size_t count = 0;
    for (size_t f = startFrame; f < startFrame + windowFrames && f < track.getNumFrames(); ++f) {
        for (int ch = 0; ch < channels; ++ch) {
            float s = track.getSample(f, ch);
            sumSq += s * s;
            count++;
        }
    }
    if (count == 0) return -100.0f;
    return static_cast<float>(10.0 * std::log10(sumSq / count + 1e-20) - 0.691);
}

float NormalizationService::measureTruePeak(const Core::AudioTrack& track) const {
    if (!track.isLoaded()) return -100.0f;
    float peak = 0.0f;
    size_t total = track.getTotalSamples();
    for (size_t i = 0; i < total; ++i) {
        peak = std::max(peak, std::abs(track.getSample(i)));
    }
    // True peak requires 4x oversampling; this is simplified
    return 20.0f * std::log10(peak + 1e-10f);
}

float NormalizationService::measureLoudnessRange(const Core::AudioTrack& track) const {
    if (!track.isLoaded()) return 0.0f;
    // Simplified LRA: difference between 95th and 10th percentile short-term loudness
    std::vector<float> shortTermValues;
    double duration = track.getDuration();
    for (double t = 0.0; t + 3.0 < duration; t += 1.0) {
        shortTermValues.push_back(measureShortTermLUFS(track, t));
    }
    if (shortTermValues.size() < 2) return 0.0f;

    std::sort(shortTermValues.begin(), shortTermValues.end());
    size_t idx10 = shortTermValues.size() / 10;
    size_t idx95 = shortTermValues.size() * 95 / 100;
    return shortTermValues[idx95] - shortTermValues[idx10];
}


NormalizationOptions NormalizationService::getPresetOptions(NormalizationPreset preset) {
    NormalizationOptions o;
    o.mode = NormalizationMode::LUFS;
    o.useLimiter = true;
    o.applyDCOffset = true;

    switch (preset) {
        case NormalizationPreset::Spotify:
            o.targetLUFS = -14.0f; o.maxTruePeakDb = -1.0f; break;
        case NormalizationPreset::AppleMusic:
            o.targetLUFS = -16.0f; o.maxTruePeakDb = -1.0f; break;
        case NormalizationPreset::YouTubeMusic:
            o.targetLUFS = -14.0f; o.maxTruePeakDb = -1.0f; break;
        case NormalizationPreset::Tidal:
            o.targetLUFS = -14.0f; o.maxTruePeakDb = -1.0f; break;
        case NormalizationPreset::Club:
            o.targetLUFS = -11.0f; o.maxTruePeakDb = -0.3f; o.limiterThreshold = -0.1f; break;
        case NormalizationPreset::Broadcast:
            o.targetLUFS = -23.0f; o.maxTruePeakDb = -1.0f; break;
        case NormalizationPreset::Podcast:
            o.targetLUFS = -16.0f; o.maxTruePeakDb = -1.0f; break;
        default: break;
    }
    return o;
}

std::string NormalizationService::presetName(NormalizationPreset preset) {
    switch (preset) {
        case NormalizationPreset::Spotify:      return "Spotify (-14 LUFS)";
        case NormalizationPreset::AppleMusic:   return "Apple Music (-16 LUFS)";
        case NormalizationPreset::YouTubeMusic: return "YouTube Music (-14 LUFS)";
        case NormalizationPreset::Tidal:        return "TIDAL (-14 LUFS)";
        case NormalizationPreset::Club:         return "Club/DJ (-11 LUFS)";
        case NormalizationPreset::Broadcast:    return "Broadcast EBU R128 (-23 LUFS)";
        case NormalizationPreset::Podcast:      return "Podcast (-16 LUFS)";
        case NormalizationPreset::Custom:       return "Custom";
    }
    return "Unknown";
}

float NormalizationService::presetTargetLUFS(NormalizationPreset preset) {
    return getPresetOptions(preset).targetLUFS;
}


void NormalizationService::removeDCOffset(std::vector<float>& samples, int channels) {
    if (samples.empty()) return;

    for (int ch = 0; ch < channels; ++ch) {
        double sum = 0.0;
        size_t count = 0;
        for (size_t i = ch; i < samples.size(); i += channels) {
            sum += samples[i];
            count++;
        }
        float dc = static_cast<float>(sum / count);
        if (std::abs(dc) > 1e-6f) {
            for (size_t i = ch; i < samples.size(); i += channels) {
                samples[i] -= dc;
            }
            spdlog::debug("NormalizationService: Removed DC offset {:.6f} from channel {}", dc, ch);
        }
    }
}

void NormalizationService::applyGain(std::vector<float>& samples, float gainLinear) {
    for (auto& s : samples) s *= gainLinear;
}

void NormalizationService::applyLimiter(std::vector<float>& samples, float thresholdDb,
                                          float releaseMs, int sampleRate, bool& limiterEngaged) {
    float threshold = std::pow(10.0f, thresholdDb / 20.0f);
    float releaseCoeff = std::exp(-1.0f / (releaseMs / 1000.0f * sampleRate));

    float envelope = 0.0f;
    limiterEngaged = false;

    for (auto& s : samples) {
        float absS = std::abs(s);
        if (absS > threshold) {
            envelope = absS;
            limiterEngaged = true;
        } else {
            envelope *= releaseCoeff;
        }

        if (envelope > threshold) {
            float gain = threshold / envelope;
            s *= gain;
        }
    }
}

} // namespace BeatMate::Services::Normalization
