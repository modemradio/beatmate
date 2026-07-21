#pragma once

#include "BPMDetector.h"
#include "BeatEngine.h"
#include "KeyDetector.h"
#include "EnergyAnalyzer.h"
#include "LoudnessAnalyzer.h"
#include "StructureDetector.h"
#include "WaveformGenerator.h"
#include "MultiBandWaveform.h"
#include "HarmonicAnalysisService.h"
#include "VocalDetectionService.h"
#include "AdvancedStructuralAnalysisService.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace BeatMate::Core {

struct TrackAnalysis {
    std::string filePath;
    BPMResult bpm;
    KeyResult key;
    EnergyResult energy;
    LoudnessResult loudness;
    std::vector<Section> structure;
    WaveformData waveform;
    MultiBandWaveformData multiBandWaveform;
    HarmonicProfile harmonic;
    VocalDetectionResult vocals;
    SelfSimilarityResult advancedStructure;
    double duration = 0.0;
    bool complete = false;
};

inline size_t safeFrameCount(size_t numSamples, size_t fftSize, size_t hopSize) {
    if (hopSize == 0 || numSamples < fftSize) return 0;
    return (numSamples - fftSize) / hopSize + 1;
}

using AnalysisProgressCallback = std::function<void(const std::string& stage, float progress)>;

class AudioAnalysisPipeline {
public:
    AudioAnalysisPipeline();
    ~AudioAnalysisPipeline();

    TrackAnalysis analyzeTrack(const std::string& path,
                               AnalysisProgressCallback progress = nullptr);

    void setAnalyzeBPM(bool v) { analyzeBPM_ = v; }
    void setAnalyzeKey(bool v) { analyzeKey_ = v; }
    void setAnalyzeEnergy(bool v) { analyzeEnergy_ = v; }
    void setAnalyzeLoudness(bool v) { analyzeLoudness_ = v; }
    void setAnalyzeStructure(bool v) { analyzeStructure_ = v; }
    void setGenerateWaveform(bool v) { generateWaveform_ = v; }
    void setAnalyzeHarmonic(bool v) { analyzeHarmonic_ = v; }
    void setAnalyzeVocals(bool v) { analyzeVocals_ = v; }
    void setAnalyzeAdvancedStructure(bool v) { analyzeAdvancedStructure_ = v; }
    void setCancelFlag(const std::atomic<bool>* flag) { cancelFlag_ = flag; }

private:
    bool isCancelled() const { return cancelFlag_ != nullptr && cancelFlag_->load(); }
    const std::atomic<bool>* cancelFlag_ = nullptr;
    bool analyzeBPM_ = true;
    bool analyzeKey_ = true;
    bool analyzeEnergy_ = true;
    bool analyzeLoudness_ = true;
    bool analyzeStructure_ = true;
    bool generateWaveform_ = true;
    bool analyzeHarmonic_ = false;
    bool analyzeVocals_ = false;
    bool analyzeAdvancedStructure_ = false;
};

} // namespace BeatMate::Core
