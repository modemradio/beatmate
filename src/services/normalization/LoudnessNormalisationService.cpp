#include "LoudnessNormalisationService.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace BeatMate::Services::Normalization {

LoudnessProfile LoudnessNormalisationService::analyzeFullProfile(const Core::AudioTrack& track) const {
    LoudnessProfile profile;
    if (!track.isLoaded()) return profile;

    profile.integratedLUFS = measureEBU_R128(track);
    profile.truePeak = 0.0f;

    size_t total = track.getTotalSamples();
    for (size_t i = 0; i < total; ++i) {
        profile.truePeak = std::max(profile.truePeak, std::abs(track.getSample(i)));
    }
    profile.truePeak = 20.0f * std::log10(profile.truePeak + 1e-10f);

    int channels = track.getChannels();
    profile.channelLUFS.resize(channels);
    profile.channelPeak.resize(channels);
    size_t numFrames = track.getNumFrames();

    for (int ch = 0; ch < channels; ++ch) {
        double sumSq = 0.0;
        float chPeak = 0.0f;
        for (size_t f = 0; f < numFrames; ++f) {
            float s = track.getSample(f, ch);
            sumSq += s * s;
            chPeak = std::max(chPeak, std::abs(s));
        }
        profile.channelLUFS[ch] = static_cast<float>(10.0 * std::log10(sumSq / numFrames + 1e-20) - 0.691);
        profile.channelPeak[ch] = 20.0f * std::log10(chPeak + 1e-10f);
    }

    profile.momentaryLUFSHistory = getMomentaryLUFSHistory(track, 100);
    profile.shortTermLUFSHistory = getShortTermLUFSHistory(track, 1000);

    if (!profile.momentaryLUFSHistory.empty()) {
        profile.momentaryMax = *std::max_element(profile.momentaryLUFSHistory.begin(),
                                                   profile.momentaryLUFSHistory.end());
    }
    if (!profile.shortTermLUFSHistory.empty()) {
        profile.shortTermMax = *std::max_element(profile.shortTermLUFSHistory.begin(),
                                                   profile.shortTermLUFSHistory.end());
    }

    profile.loudnessRange = measureDynamicRange(track);
    profile.dynamicRange = measureCrestFactor(track);
    profile.replayGainDb = measureReplayGain(track);

    spdlog::info("LoudnessNormalisationService: Profile - LUFS: {:.1f}, Peak: {:.1f} dBTP, LRA: {:.1f} LU",
                 profile.integratedLUFS, profile.truePeak, profile.loudnessRange);
    return profile;
}

MultiAlgoResult LoudnessNormalisationService::analyzeMultiAlgorithm(const Core::AudioTrack& track,
                                                                       const MultiAlgoOptions& options) const {
    MultiAlgoResult result;
    if (!track.isLoaded()) return result;

    result.profile = analyzeFullProfile(track);
    result.ebuR128LUFS = result.profile.integratedLUFS;
    result.bs1770LUFS = measureBS1770(track);
    result.replayGainDb = measureReplayGain(track);
    result.kSystemLevel = measureKSystem(track);

    size_t total = track.getTotalSamples();
    double sumSq = 0.0;
    float peak = 0.0f;
    for (size_t i = 0; i < total; ++i) {
        float s = track.getSample(i);
        sumSq += s * s;
        peak = std::max(peak, std::abs(s));
    }
    result.rmsDb = static_cast<float>(20.0 * std::log10(std::sqrt(sumSq / total) + 1e-10));
    result.peakDb = 20.0f * std::log10(peak + 1e-10f);

    result.success = true;
    return result;
}

MultiAlgoResult LoudnessNormalisationService::normalize(Core::AudioTrack& track,
                                                          const MultiAlgoOptions& options) {
    auto result = analyzeMultiAlgorithm(track, options);
    if (!result.success) return result;

    float currentLoudness = 0.0f;
    switch (options.primaryAlgorithm) {
        case LoudnessAlgorithm::BS1770:       currentLoudness = result.bs1770LUFS; break;
        case LoudnessAlgorithm::EBU_R128:     currentLoudness = result.ebuR128LUFS; break;
        case LoudnessAlgorithm::ReplayGain2:  currentLoudness = -18.0f - result.replayGainDb; break;
        case LoudnessAlgorithm::K_System:     currentLoudness = result.kSystemLevel; break;
        case LoudnessAlgorithm::SimpleRMS:    currentLoudness = result.rmsDb; break;
        case LoudnessAlgorithm::PeakNormalize: currentLoudness = result.peakDb; break;
    }

    float gainDb = options.targetLoudness - currentLoudness;
    float gainLinear = std::pow(10.0f, gainDb / 20.0f);
    result.appliedGainDb = gainDb;

    size_t total = track.getTotalSamples();
    std::vector<float> samples(total);
    for (size_t i = 0; i < total; ++i) {
        samples[i] = track.getSample(i) * gainLinear;
    }

    float tpThreshold = std::pow(10.0f, options.maxTruePeakDbTP / 20.0f);
    bool limited = false;
    for (auto& s : samples) {
        if (std::abs(s) > tpThreshold) {
            s = (s > 0) ? tpThreshold : -tpThreshold;
            limited = true;
        }
    }

    track.loadData(std::move(samples), track.getSampleRate(), track.getChannels());

    spdlog::info("LoudnessNormalisationService: Normalized using {} - gain: {:.1f} dB{}",
                 algorithmName(options.primaryAlgorithm), gainDb, limited ? " (limited)" : "");
    return result;
}

float LoudnessNormalisationService::measureBS1770(const Core::AudioTrack& track) const {
    if (!track.isLoaded()) return -100.0f;

    size_t total = track.getTotalSamples();
    int channels = track.getChannels();
    size_t numFrames = track.getNumFrames();

    double totalPower = 0.0;
    for (size_t f = 0; f < numFrames; ++f) {
        double framePower = 0.0;
        for (int ch = 0; ch < channels; ++ch) {
            float s = track.getSample(f, ch);
            float weight = 1.0f; // Simplified for stereo
            framePower += s * s * weight;
        }
        totalPower += framePower;
    }

    double meanPower = totalPower / numFrames;
    return static_cast<float>(10.0 * std::log10(meanPower + 1e-20) - 0.691);
}

float LoudnessNormalisationService::measureReplayGain(const Core::AudioTrack& track) const {
    float lufs = measureEBU_R128(track);
    return -18.0f - lufs; // Gain needed to reach -18 LUFS
}

float LoudnessNormalisationService::measureEBU_R128(const Core::AudioTrack& track) const {
    if (!track.isLoaded()) return -100.0f;

    int channels = track.getChannels();
    int sampleRate = track.getSampleRate();
    size_t numFrames = track.getNumFrames();

    size_t blockFrames = static_cast<size_t>(0.4 * sampleRate);
    size_t hopFrames = blockFrames / 4;

    std::vector<double> blockPowers;

    for (size_t start = 0; start + blockFrames <= numFrames; start += hopFrames) {
        double power = 0.0;
        for (size_t f = start; f < start + blockFrames; ++f) {
            for (int ch = 0; ch < channels; ++ch) {
                float s = track.getSample(f, ch);
                power += s * s;
            }
        }
        power /= (blockFrames * channels);
        blockPowers.push_back(power);
    }

    if (blockPowers.empty()) return -100.0f;

    double absGateThreshold = std::pow(10.0, (-70.0 + 0.691) / 10.0);
    double sumAbove = 0.0;
    int countAbove = 0;
    for (double bp : blockPowers) {
        if (bp > absGateThreshold) { sumAbove += bp; countAbove++; }
    }
    if (countAbove == 0) return -100.0f;

    double absGateLevel = 10.0 * std::log10(sumAbove / countAbove) - 0.691;
    double relGateThreshold = std::pow(10.0, (absGateLevel - 10.0 + 0.691) / 10.0);

    double sumFinal = 0.0;
    int countFinal = 0;
    for (double bp : blockPowers) {
        if (bp > relGateThreshold) { sumFinal += bp; countFinal++; }
    }

    if (countFinal == 0) return -100.0f;
    return static_cast<float>(10.0 * std::log10(sumFinal / countFinal) - 0.691);
}

float LoudnessNormalisationService::measureKSystem(const Core::AudioTrack& track, int kLevel) const {
    float rms = 0.0f;
    size_t total = track.getTotalSamples();
    if (total == 0) return -100.0f;

    double sumSq = 0.0;
    for (size_t i = 0; i < total; ++i) {
        float s = track.getSample(i);
        sumSq += s * s;
    }
    rms = static_cast<float>(20.0 * std::log10(std::sqrt(sumSq / total) + 1e-10));

    return rms + static_cast<float>(kLevel);
}

float LoudnessNormalisationService::measureDynamicRange(const Core::AudioTrack& track) const {
    auto history = getShortTermLUFSHistory(track, 1000);
    if (history.size() < 2) return 0.0f;

    std::vector<float> valid;
    for (float v : history) if (v > -60.0f) valid.push_back(v);
    if (valid.size() < 2) return 0.0f;

    std::sort(valid.begin(), valid.end());
    size_t idx10 = valid.size() * 10 / 100;
    size_t idx95 = valid.size() * 95 / 100;
    return valid[idx95] - valid[idx10];
}

float LoudnessNormalisationService::measureCrestFactor(const Core::AudioTrack& track) const {
    if (!track.isLoaded()) return 0.0f;
    size_t total = track.getTotalSamples();

    float peak = 0.0f;
    double sumSq = 0.0;
    for (size_t i = 0; i < total; ++i) {
        float s = track.getSample(i);
        peak = std::max(peak, std::abs(s));
        sumSq += s * s;
    }
    float rms = std::sqrt(static_cast<float>(sumSq / total));
    if (rms < 1e-10f) return 0.0f;
    return 20.0f * std::log10(peak / rms);
}

std::vector<float> LoudnessNormalisationService::getMomentaryLUFSHistory(
    const Core::AudioTrack& track, int resolutionMs) const {

    std::vector<float> history;
    if (!track.isLoaded()) return history;

    int sampleRate = track.getSampleRate();
    int channels = track.getChannels();
    size_t numFrames = track.getNumFrames();

    size_t windowFrames = static_cast<size_t>(0.4 * sampleRate); // 400ms
    size_t hopFrames = static_cast<size_t>(resolutionMs / 1000.0 * sampleRate);

    for (size_t start = 0; start + windowFrames <= numFrames; start += hopFrames) {
        double power = 0.0;
        for (size_t f = start; f < start + windowFrames; ++f) {
            for (int ch = 0; ch < channels; ++ch) {
                float s = track.getSample(f, ch);
                power += s * s;
            }
        }
        power /= (windowFrames * channels);
        float lufs = static_cast<float>(10.0 * std::log10(power + 1e-20) - 0.691);
        history.push_back(lufs);
    }
    return history;
}

std::vector<float> LoudnessNormalisationService::getShortTermLUFSHistory(
    const Core::AudioTrack& track, int resolutionMs) const {

    std::vector<float> history;
    if (!track.isLoaded()) return history;

    int sampleRate = track.getSampleRate();
    int channels = track.getChannels();
    size_t numFrames = track.getNumFrames();

    size_t windowFrames = static_cast<size_t>(3.0 * sampleRate); // 3-second window
    size_t hopFrames = static_cast<size_t>(resolutionMs / 1000.0 * sampleRate);

    for (size_t start = 0; start + windowFrames <= numFrames; start += hopFrames) {
        double power = 0.0;
        for (size_t f = start; f < start + windowFrames; ++f) {
            for (int ch = 0; ch < channels; ++ch) {
                float s = track.getSample(f, ch);
                power += s * s;
            }
        }
        power /= (windowFrames * channels);
        float lufs = static_cast<float>(10.0 * std::log10(power + 1e-20) - 0.691);
        history.push_back(lufs);
    }
    return history;
}

std::string LoudnessNormalisationService::algorithmName(LoudnessAlgorithm algo) {
    switch (algo) {
        case LoudnessAlgorithm::BS1770:         return "ITU-R BS.1770";
        case LoudnessAlgorithm::ReplayGain2:    return "ReplayGain 2.0";
        case LoudnessAlgorithm::EBU_R128:       return "EBU R128";
        case LoudnessAlgorithm::K_System:       return "K-System";
        case LoudnessAlgorithm::SimpleRMS:      return "Simple RMS";
        case LoudnessAlgorithm::PeakNormalize:  return "Peak Normalize";
    }
    return "Unknown";
}

float LoudnessNormalisationService::luToDb(float lu) { return lu; }
float LoudnessNormalisationService::dbToLu(float db) { return db; }

std::vector<float> LoudnessNormalisationService::kWeightFilter(const std::vector<float>& samples,
                                                                  int /*sampleRate*/,
                                                                  int /*channels*/) const {
    return samples; // Passthrough for simplification
}

} // namespace BeatMate::Services::Normalization
