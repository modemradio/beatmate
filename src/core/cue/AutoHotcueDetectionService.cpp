#include "AutoHotcueDetectionService.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include "../analysis/BPMDetector.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

AutoHotcueDetectionService::AutoHotcueDetectionService() = default;
AutoHotcueDetectionService::~AutoHotcueDetectionService() = default;

std::vector<AutoDetectedCue> AutoHotcueDetectionService::findEnergyPeaks(
    const AudioTrack& track, double barDuration) {

    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    std::vector<AutoDetectedCue> cues;

    size_t windowSize = static_cast<size_t>(barDuration * sr);
    if (windowSize == 0) return cues;

    size_t numWindows = numSamples / windowSize;

    std::vector<float> barEnergy;
    for (size_t w = 0; w < numWindows; ++w) {
        const float* barData = data + w * windowSize;
        float rms = 0.0f;
        for (size_t i = 0; i < windowSize; ++i) rms += barData[i] * barData[i];
        barEnergy.push_back(std::sqrt(rms / windowSize));
    }

    for (size_t i = 2; i < barEnergy.size(); ++i) {
        float prevAvg = (barEnergy[i - 1] + barEnergy[i - 2]) / 2.0f;
        float ratio = (prevAvg > 1e-6f) ? barEnergy[i] / prevAvg : 0.0f;

        if (ratio > 1.8f) {
            AutoDetectedCue cue;
            cue.cue.position = i * barDuration;
            cue.cue.color = 0xFFFF4500;
            cue.cue.name = "Energy Peak";
            cue.detectionMethod = "energy_peak";
            cue.score = std::clamp((ratio - 1.0f) / 2.0f, 0.0f, 1.0f);
            cue.description = "Significant energy increase detected";
            cues.push_back(cue);
        }
    }

    return cues;
}

std::vector<AutoDetectedCue> AutoHotcueDetectionService::findSpectralChanges(
    const AudioTrack& track, double barDuration) {

    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    std::vector<AutoDetectedCue> cues;

    int fftSize = 2048;
    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    size_t windowSize = static_cast<size_t>(barDuration * sr);
    if (windowSize < static_cast<size_t>(fftSize)) return cues;

    size_t numWindows = numSamples / windowSize;

    std::vector<float> centroids;
    for (size_t w = 0; w < numWindows; ++w) {
        size_t center = w * windowSize + windowSize / 2;
        if (center + fftSize / 2 >= numSamples) break;

        std::vector<std::complex<float>> spectrum;
        fft.forward(data + center - fftSize / 2, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        float weightedSum = 0.0f, magSum = 0.0f;
        for (int bin = 1; bin < static_cast<int>(mag.size()); ++bin) {
            float freq = static_cast<float>(bin) * sr / fftSize;
            weightedSum += freq * mag[bin];
            magSum += mag[bin];
        }
        centroids.push_back((magSum > 0) ? weightedSum / magSum : 0.0f);
    }

    for (size_t i = 2; i < centroids.size(); ++i) {
        float prevAvg = (centroids[i - 1] + centroids[i - 2]) / 2.0f;
        float diff = std::fabs(centroids[i] - prevAvg);
        float relDiff = (prevAvg > 1.0f) ? diff / prevAvg : 0.0f;

        if (relDiff > 0.3f) {
            AutoDetectedCue cue;
            cue.cue.position = i * barDuration;
            cue.cue.color = 0xFF9370DB;
            cue.cue.name = "Spectral Change";
            cue.detectionMethod = "spectral_change";
            cue.score = std::clamp(relDiff, 0.0f, 1.0f);
            cue.description = "Significant timbre change detected";
            cues.push_back(cue);
        }
    }

    return cues;
}

std::vector<AutoDetectedCue> AutoHotcueDetectionService::findSignificantOnsets(
    const AudioTrack& track, double barDuration) {

    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    std::vector<AutoDetectedCue> cues;

    size_t windowSize = static_cast<size_t>(barDuration * sr);
    if (windowSize == 0) return cues;

    float silenceThreshold = 0.01f;
    bool wasSilent = true;

    for (size_t pos = 0; pos + windowSize < numSamples; pos += windowSize) {
        float rms = 0.0f;
        for (size_t i = 0; i < windowSize; ++i) {
            rms += data[pos + i] * data[pos + i];
        }
        rms = std::sqrt(rms / windowSize);

        if (wasSilent && rms > silenceThreshold * 5.0f) {
            AutoDetectedCue cue;
            cue.cue.position = static_cast<double>(pos) / sr;
            cue.cue.color = 0xFF00FF00;
            cue.cue.name = "Post-Silence Onset";
            cue.detectionMethod = "onset";
            cue.score = std::clamp(rms * 5.0f, 0.0f, 1.0f);
            cue.description = "First significant sound after quiet section";
            cues.push_back(cue);
        }

        wasSilent = (rms < silenceThreshold);
    }

    return cues;
}

std::vector<AutoDetectedCue> AutoHotcueDetectionService::findSilenceEnds(const AudioTrack& track) {
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    std::vector<AutoDetectedCue> cues;

    int windowSize = sr / 10; // 100ms windows
    float threshold = 0.005f;
    bool inSilence = true;
    int silenceCount = 0;

    for (size_t pos = 0; pos + windowSize < numSamples; pos += windowSize) {
        float rms = 0.0f;
        for (int i = 0; i < windowSize; ++i) {
            rms += data[pos + i] * data[pos + i];
        }
        rms = std::sqrt(rms / windowSize);

        if (rms < threshold) {
            inSilence = true;
            silenceCount++;
        } else if (inSilence && rms >= threshold) {
            inSilence = false;
            if (silenceCount > 5) {
                AutoDetectedCue cue;
                cue.cue.position = static_cast<double>(pos) / sr;
                cue.cue.color = 0xFF00BFFF;
                cue.cue.name = "After Silence";
                cue.detectionMethod = "silence_end";
                cue.score = std::clamp(static_cast<float>(silenceCount) / 20.0f, 0.3f, 1.0f);
                cue.description = "Sound begins after silence";
                cues.push_back(cue);
            }
            silenceCount = 0;
        }
    }

    return cues;
}

std::vector<AutoDetectedCue> AutoHotcueDetectionService::deduplicateAndSelect(
    const std::vector<AutoDetectedCue>& candidates, int maxCues, double minDistance) {

    auto sorted = candidates;
    std::sort(sorted.begin(), sorted.end(),
              [](const AutoDetectedCue& a, const AutoDetectedCue& b) { return a.score > b.score; });

    std::vector<AutoDetectedCue> selected;
    for (auto& candidate : sorted) {
        if (static_cast<int>(selected.size()) >= maxCues) break;

        bool tooClose = false;
        for (auto& sel : selected) {
            if (std::fabs(sel.cue.position - candidate.cue.position) < minDistance) {
                tooClose = true;
                break;
            }
        }

        if (!tooClose) {
            selected.push_back(candidate);
        }
    }

    std::sort(selected.begin(), selected.end(),
              [](const AutoDetectedCue& a, const AutoDetectedCue& b) {
                  return a.cue.position < b.cue.position;
              });

    for (int i = 0; i < static_cast<int>(selected.size()); ++i) {
        selected[i].cue.number = i + 1;
    }

    return selected;
}

AutoHotcueResult AutoHotcueDetectionService::detect(const AudioTrack& track, int maxCues, double bpm) {
    spdlog::info("AutoHotcueDetectionService: detecting cues for {}", track.getFilePath());

    AutoHotcueResult result;

    if (bpm <= 0) {
        BPMDetector detector;
        bpm = detector.detect(track).bpm;
    }

    double barDuration = 240.0 / bpm;

    std::vector<AutoDetectedCue> allCandidates;

    if (detectEnergyPeaks_) {
        auto peaks = findEnergyPeaks(track, barDuration);
        allCandidates.insert(allCandidates.end(), peaks.begin(), peaks.end());
    }

    if (detectSpectralChanges_) {
        auto changes = findSpectralChanges(track, barDuration);
        allCandidates.insert(allCandidates.end(), changes.begin(), changes.end());
    }

    if (detectOnsets_) {
        auto onsets = findSignificantOnsets(track, barDuration);
        allCandidates.insert(allCandidates.end(), onsets.begin(), onsets.end());
    }

    if (detectSilenceEnd_) {
        auto silenceEnds = findSilenceEnds(track);
        allCandidates.insert(allCandidates.end(), silenceEnds.begin(), silenceEnds.end());
    }

    double minDistance = barDuration * 4; // At least 4 bars apart
    result.detectedCues = deduplicateAndSelect(allCandidates, maxCues, minDistance);
    result.suggestedCount = static_cast<int>(result.detectedCues.size());

    if (!result.detectedCues.empty()) {
        float scoreSum = 0.0f;
        for (auto& c : result.detectedCues) scoreSum += c.score;
        result.overallConfidence = scoreSum / result.detectedCues.size();
    }

    spdlog::info("AutoHotcueDetectionService: detected {} cues, confidence {:.0f}%",
                 result.suggestedCount, result.overallConfidence * 100);
    return result;
}

} // namespace BeatMate::Core
