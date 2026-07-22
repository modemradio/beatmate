#include "AutoAnalysisService.h"
#include "../audio/AudioTrack.h"
#include "BPMDetector.h"
#include "KeyDetector.h"
#include "WaveformGenerator.h"
#include "MultiBandWaveform.h"
#include <spdlog/spdlog.h>
#include <chrono>

namespace BeatMate::Core {

AutoAnalysisService::AutoAnalysisService() = default;
AutoAnalysisService::~AutoAnalysisService() = default;

FullTrackAnalysis AutoAnalysisService::analyzeTrack(const std::string& filePath,
                                                      FullAnalysisProgressCallback progress) {
    auto startTime = std::chrono::steady_clock::now();
    cancelled_.store(false);

    FullTrackAnalysis result;
    result.filePath = filePath;

    spdlog::info("AutoAnalysisService: starting full analysis of {}", filePath);

    AudioAnalysisPipeline pipeline;
    auto basicAnalysis = pipeline.analyzeTrack(filePath, [&](const std::string& stage, float p) {
        if (progress) progress("Loading: " + stage, p * 0.1f);
    });

    if (!basicAnalysis.complete) {
        result.errorMessage = "Failed to load audio file";
        spdlog::error("AutoAnalysisService: failed to load {}", filePath);
        return result;
    }

    result.duration = basicAnalysis.duration;
    result.waveform = basicAnalysis.waveform;
    result.multiBandWaveform = basicAnalysis.multiBandWaveform;

    if (isCancelled()) return result;

    float progressBase = 0.1f;
    if (analyzeBPM_) {
        if (progress) progress("BPM Detection", progressBase);
        spdlog::info("AutoAnalysisService: BPM analysis");

        result.bpm.bpm = basicAnalysis.bpm.bpm;
        result.bpm.beats = basicAnalysis.bpm.beats;
        result.bpm.offset = basicAnalysis.bpm.offset;
        result.bpm.confidence = basicAnalysis.bpm.confidence;
        result.bpm.passCount = 1;
        result.bpm.passBPMs.push_back(basicAnalysis.bpm.bpm);
    }
    progressBase = 0.2f;

    if (isCancelled()) return result;

    if (analyzeKey_) {
        if (progress) progress("Key Detection", progressBase);
        spdlog::info("AutoAnalysisService: key analysis");
        result.key = basicAnalysis.key;

        KeyFormatsService keyFormats;
        result.keyInfo = keyFormats.fromStandard(result.key.key);
    }
    progressBase = 0.3f;

    if (isCancelled()) return result;

    if (analyzeEnergy_) {
        if (progress) progress("Energy Analysis", progressBase);
        spdlog::info("AutoAnalysisService: energy analysis");

        result.energy.overallEnergy = basicAnalysis.energy.overall;
        result.energy.rmsGlobal = basicAnalysis.energy.rmsAverage;
        result.energy.spectralCentroid = basicAnalysis.energy.spectralCentroid;
        result.energy.energyCurve = basicAnalysis.energy.curve;
    }
    progressBase = 0.4f;

    if (isCancelled()) return result;

    if (analyzeLoudness_) {
        if (progress) progress("Loudness Analysis", progressBase);
        spdlog::info("AutoAnalysisService: loudness analysis");

        result.loudness.integratedLUFS = basicAnalysis.loudness.integratedLUFS;
        result.loudness.truePeakdBTP = basicAnalysis.loudness.truePeakdBTP;
        result.loudness.loudnessRange = basicAnalysis.loudness.loudnessRange;
    }
    progressBase = 0.5f;

    if (isCancelled()) return result;

    if (analyzeStructure_) {
        if (progress) progress("Structure Analysis", progressBase);
        spdlog::info("AutoAnalysisService: structure analysis");

        SongStructureAnalyzerService structAnalyzer;
        result.structure = structAnalyzer.analyze(
            *static_cast<const AudioTrack*>(nullptr),
            result.bpm.bpm);

        if (result.structure.sections.empty()) {
            for (auto& sec : basicAnalysis.structure) {
                SongSection songSec;
                songSec.type = sec.type;
                songSec.startTime = sec.startTime;
                songSec.endTime = sec.endTime;
                songSec.label = sec.label;
                songSec.confidence = sec.confidence;
                result.structure.sections.push_back(songSec);
            }
        }
    }
    progressBase = 0.7f;

    if (isCancelled()) return result;

    if (analyzePhrases_ && result.bpm.bpm > 0) {
        if (progress) progress("Phrase Analysis", progressBase);
        spdlog::info("AutoAnalysisService: phrase analysis");
    }
    progressBase = 0.8f;

    if (isCancelled()) return result;

    if (analyzeIntroOutro_) {
        if (progress) progress("Intro/Outro Detection", progressBase);
        spdlog::info("AutoAnalysisService: intro/outro detection");
    }
    progressBase = 0.9f;

    if (isCancelled()) return result;

    if (analyzeDownbeats_ && !result.bpm.beats.empty()) {
        if (progress) progress("Downbeat Detection", progressBase);
        spdlog::info("AutoAnalysisService: downbeat detection");
    }

    result.complete = true;

    auto endTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    if (progress) progress("Complete", 1.0f);

    spdlog::info("AutoAnalysisService: complete in {}ms - BPM={:.1f}, Key={}, Energy={}/10, LUFS={:.1f}",
                 elapsed.count(), result.bpm.bpm, result.key.key,
                 result.energy.overallEnergy, result.loudness.integratedLUFS);
    return result;
}

}
