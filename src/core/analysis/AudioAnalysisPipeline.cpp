#include "AudioAnalysisPipeline.h"
#include "../audio/AudioFileReader.h"
#include "../audio/AudioTrack.h"
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

AudioAnalysisPipeline::AudioAnalysisPipeline() = default;
AudioAnalysisPipeline::~AudioAnalysisPipeline() = default;

TrackAnalysis AudioAnalysisPipeline::analyzeTrack(const std::string& path,
                                                   AnalysisProgressCallback progress) {
    TrackAnalysis result;
    result.filePath = path;

    spdlog::info("AudioAnalysisPipeline: starting analysis of {}", path);

    if (isCancelled()) return result;
    if (progress) progress("Loading", 0.0f);
    AudioFileReader reader;
    auto track = reader.readFile(path);
    if (!track) {
        spdlog::error("AudioAnalysisPipeline: failed to load {}", path);
        return result;
    }

    result.duration = track->getDuration();
    if (progress) progress("Loading", 1.0f);

    // Guard: skip analysis on empty/corrupted tracks to avoid segfaults in
    if (track->getTotalSamples() == 0 || track->getSampleRate() <= 0
        || track->getChannels() <= 0) {
        spdlog::warn("AudioAnalysisPipeline: track has no audio data (samples={}, sr={}, ch={}); skipping analysis for {}",
                     track->getTotalSamples(), track->getSampleRate(),
                     track->getChannels(), path);
        return result;
    }

    int step = 0;
    const int totalSteps = (analyzeBPM_ ? 1 : 0) + (analyzeKey_ ? 1 : 0) +
                           (analyzeEnergy_ ? 1 : 0) + (analyzeLoudness_ ? 1 : 0) +
                           (analyzeStructure_ ? 1 : 0) + (generateWaveform_ ? 1 : 0) +
                           (analyzeHarmonic_ ? 1 : 0) + (analyzeVocals_ ? 1 : 0) +
                           (analyzeAdvancedStructure_ ? 1 : 0);

    // Guard against div-by-zero when caller disables every stage.
    auto pct = [&](int s) -> float {
        return totalSteps > 0 ? static_cast<float>(s) / static_cast<float>(totalSteps) : 1.0f;
    };

    if (analyzeBPM_ && !isCancelled()) {
        if (progress) progress("BPM Detection", pct(step));
        BeatEngine engine;
        BeatEngineOptions opts;
        BeatGridCore grid = engine.analyze(*track, opts);
        result.bpm.bpm = grid.bpm;
        result.bpm.confidence = grid.confidence;
        result.bpm.beats = grid.beats;
        result.bpm.offset = grid.firstDownbeatSec;
        step++;
    }

    if (analyzeKey_ && !isCancelled()) {
        if (progress) progress("Key Detection", pct(step));
        KeyDetector keyDet;
        result.key = keyDet.detect(*track);
        step++;
    }

    if (analyzeEnergy_ && !isCancelled()) {
        if (progress) progress("Energy Analysis", pct(step));
        EnergyAnalyzer energyAn;
        result.energy = energyAn.analyze(*track);
        step++;
    }

    if (analyzeLoudness_ && !isCancelled()) {
        if (progress) progress("Loudness Analysis", pct(step));
        LoudnessAnalyzer loudnessAn;
        result.loudness = loudnessAn.analyze(*track);
        step++;
    }

    if (analyzeStructure_ && !isCancelled()) {
        if (progress) progress("Structure Detection", pct(step));
        StructureDetector structDet;
        result.structure = structDet.detect(*track);
        step++;
    }

    if (generateWaveform_ && !isCancelled()) {
        if (progress) progress("Waveform Generation", pct(step));
        WaveformGenerator wfGen;
        result.waveform = wfGen.generate(*track);

        MultiBandWaveform mbWf;
        result.multiBandWaveform = mbWf.generate(*track);
        step++;
    }

    if (analyzeHarmonic_ && !isCancelled()) {
        if (progress) progress("Harmonic Analysis", pct(step));
        HarmonicAnalysisService harmonic;
        result.harmonic = harmonic.analyze(*track);
        step++;
    }

    if (analyzeVocals_ && !isCancelled()) {
        if (progress) progress("Vocal Detection", pct(step));
        VocalDetectionService vocal;
        result.vocals = vocal.detect(*track);
        step++;
    }

    if (analyzeAdvancedStructure_ && !isCancelled()) {
        if (progress) progress("Structural SSM", pct(step));
        AdvancedStructuralAnalysisService adv;
        result.advancedStructure = adv.analyze(*track);
        step++;
    }

    if (isCancelled()) return result;

    result.complete = true;
    if (progress) progress("Complete", 1.0f);

    spdlog::info("AudioAnalysisPipeline: analysis complete for {}", path);
    return result;
}

} // namespace BeatMate::Core
