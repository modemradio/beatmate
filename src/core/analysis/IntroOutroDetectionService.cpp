#include "IntroOutroDetectionService.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include "BPMDetector.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

IntroOutroDetectionService::IntroOutroDetectionService() = default;
IntroOutroDetectionService::~IntroOutroDetectionService() = default;

double IntroOutroDetectionService::findEnergyRise(const std::vector<float>& energyProfile,
                                                    double barDuration) {
    if (energyProfile.size() < 4) return 0.0;

    int windowSize = 4; // 4 bars

    for (int bar = 0; bar + windowSize < static_cast<int>(energyProfile.size()); ++bar) {
        float beforeEnergy = 0.0f;
        float afterEnergy = 0.0f;

        for (int i = std::max(0, bar - windowSize); i < bar; ++i) {
            beforeEnergy += energyProfile[i];
        }
        for (int i = bar; i < bar + windowSize; ++i) {
            afterEnergy += energyProfile[i];
        }

        int beforeCount = std::min(windowSize, bar);
        if (beforeCount > 0) beforeEnergy /= beforeCount;
        afterEnergy /= windowSize;

        if (afterEnergy > energyThreshold_ &&
            (beforeCount == 0 || afterEnergy > beforeEnergy * 2.0f)) {
            return bar * barDuration;
        }
    }

    return 0.0;
}

double IntroOutroDetectionService::findEnergyDrop(const std::vector<float>& energyProfile,
                                                    double barDuration) {
    if (energyProfile.size() < 4) return energyProfile.size() * barDuration;

    int windowSize = 4;
    int totalBars = static_cast<int>(energyProfile.size());

    for (int bar = totalBars - 1; bar - windowSize >= 0; --bar) {
        float beforeEnergy = 0.0f;
        float afterEnergy = 0.0f;

        for (int i = bar - windowSize; i < bar; ++i) {
            beforeEnergy += energyProfile[i];
        }
        for (int i = bar; i < std::min(totalBars, bar + windowSize); ++i) {
            afterEnergy += energyProfile[i];
        }

        beforeEnergy /= windowSize;
        int afterCount = std::min(windowSize, totalBars - bar);
        if (afterCount > 0) afterEnergy /= afterCount;

        if (beforeEnergy > energyThreshold_ &&
            (afterEnergy < beforeEnergy * 0.5f || afterEnergy < energyThreshold_)) {
            return bar * barDuration;
        }
    }

    return totalBars * barDuration;
}

std::string IntroOutroDetectionService::classifyType(const AudioTrack& track, double start, double end) {
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t totalSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    size_t startSample = static_cast<size_t>(start * sr);
    size_t endSample = std::min(static_cast<size_t>(end * sr), totalSamples);
    size_t len = endSample - startSample;

    if (len < 1024) return "silence";

    double rmsSum = 0.0;
    for (size_t i = startSample; i < endSample; ++i) {
        rmsSum += data[i] * data[i];
    }
    float rms = static_cast<float>(std::sqrt(rmsSum / len));

    if (rms < 0.01f) return "silence";

    int fftSize = 2048;
    if (len < static_cast<size_t>(fftSize)) return "ambient";

    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    std::vector<std::complex<float>> spectrum;
    fft.forward(data + startSample, spectrum);
    auto mag = fft.getMagnitudes(spectrum);

    float bassEnergy = 0.0f, totalEnergy = 0.0f;
    for (int bin = 0; bin < static_cast<int>(mag.size()); ++bin) {
        float freq = static_cast<float>(bin) * sr / fftSize;
        totalEnergy += mag[bin] * mag[bin];
        if (freq >= 30.0f && freq <= 200.0f) bassEnergy += mag[bin] * mag[bin];
    }

    float bassRatio = (totalEnergy > 0) ? bassEnergy / totalEnergy : 0.0f;

    if (bassRatio > 0.4f) return "beat";
    if (rms > 0.1f) return "buildup";
    return "ambient";
}

IntroOutroResult IntroOutroDetectionService::detect(const AudioTrack& track, double bpm) {
    spdlog::info("IntroOutroDetectionService: analyzing {}", track.getFilePath());

    IntroOutroResult result;

    if (bpm <= 0) {
        BPMDetector detector;
        bpm = detector.detect(track).bpm;
    }

    double beatDuration = 60.0 / bpm;
    double barDuration = beatDuration * 4.0;

    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t totalSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    size_t barSamples = static_cast<size_t>(barDuration * sr);
    int numBars = static_cast<int>(totalSamples / barSamples);

    std::vector<float> energyProfile;
    for (int bar = 0; bar < numBars; ++bar) {
        const float* barData = data + bar * barSamples;
        size_t len = std::min(barSamples, totalSamples - bar * barSamples);
        double sum = 0.0;
        for (size_t i = 0; i < len; ++i) sum += barData[i] * barData[i];
        energyProfile.push_back(static_cast<float>(std::sqrt(sum / len)));
    }

    float maxEnergy = 0.0f;
    for (auto& e : energyProfile) maxEnergy = std::max(maxEnergy, e);
    if (maxEnergy > 0) {
        for (auto& e : energyProfile) e /= maxEnergy;
    }

    result.introStart = 0.0;
    result.introEnd = findEnergyRise(energyProfile, barDuration);
    result.introBars = static_cast<int>(result.introEnd / barDuration);
    result.introBars = std::min(result.introBars, maxIntroBars_);
    result.introEnd = result.introBars * barDuration;

    result.outroStart = findEnergyDrop(energyProfile, barDuration);
    result.outroEnd = track.getDuration();
    result.outroBars = static_cast<int>((result.outroEnd - result.outroStart) / barDuration);
    result.outroBars = std::min(result.outroBars, maxIntroBars_);
    result.outroStart = result.outroEnd - result.outroBars * barDuration;

    if (result.introBars > 0) {
        float sum = 0.0f;
        for (int i = 0; i < result.introBars && i < static_cast<int>(energyProfile.size()); ++i) {
            sum += energyProfile[i];
        }
        result.introEnergy = sum / result.introBars;
    }

    if (result.outroBars > 0) {
        int outroStartBar = static_cast<int>(result.outroStart / barDuration);
        float sum = 0.0f;
        int count = 0;
        for (int i = outroStartBar; i < numBars; ++i) {
            sum += energyProfile[i];
            count++;
        }
        result.outroEnergy = (count > 0) ? sum / count : 0.0f;
    }

    result.introType = classifyType(track, result.introStart, result.introEnd);
    result.outroType = classifyType(track, result.outroStart, result.outroEnd);

    result.mixInPoint = result.introEnd;
    result.mixOutPoint = result.outroStart;

    result.mixInPoint = std::round(result.mixInPoint / barDuration) * barDuration;
    result.mixOutPoint = std::round(result.mixOutPoint / barDuration) * barDuration;

    result.confidence = 0.8f;

    spdlog::info("IntroOutroDetectionService: intro={:.1f}s ({}bars, {}), outro={:.1f}s ({}bars, {})",
                 result.introEnd, result.introBars, result.introType,
                 result.outroEnd - result.outroStart, result.outroBars, result.outroType);
    return result;
}

} // namespace BeatMate::Core
