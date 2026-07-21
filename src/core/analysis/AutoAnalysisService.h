#pragma once

#include "AudioAnalysisPipeline.h"
#include "UltraPrecisionBPMService.h"
#include "KeyFormatsService.h"
#include "RealEnergyAnalysisService.h"
#include "EbuR128AnalyzerService.h"
#include "SongStructureAnalyzerService.h"
#include "PhraseAnalyzerService.h"
#include "IntroOutroDetectionService.h"
#include "DownbeatDetectionService.h"
#include <functional>
#include <string>
#include <memory>
#include <vector>
#include <atomic>

namespace BeatMate::Core {

struct FullTrackAnalysis {
    std::string filePath;
    UltraPrecisionBPMResult bpm;
    KeyResult key;
    KeyInfo keyInfo;
    RealEnergyResult energy;
    EbuR128Result loudness;
    SongStructure structure;
    PhraseAnalysisResult phrases;
    IntroOutroResult introOutro;
    DownbeatResult downbeats;
    WaveformData waveform;
    MultiBandWaveformData multiBandWaveform;
    double duration = 0.0;
    bool complete = false;
    std::string errorMessage;
};

using FullAnalysisProgressCallback = std::function<void(const std::string& stage, float progress)>;

class AutoAnalysisService {
public:
    AutoAnalysisService();
    ~AutoAnalysisService();

    FullTrackAnalysis analyzeTrack(const std::string& filePath,
                                    FullAnalysisProgressCallback progress = nullptr);

    void cancel() { cancelled_.store(true); }
    bool isCancelled() const { return cancelled_.load(); }

    void setAnalyzeBPM(bool v) { analyzeBPM_ = v; }
    void setAnalyzeKey(bool v) { analyzeKey_ = v; }
    void setAnalyzeEnergy(bool v) { analyzeEnergy_ = v; }
    void setAnalyzeLoudness(bool v) { analyzeLoudness_ = v; }
    void setAnalyzeStructure(bool v) { analyzeStructure_ = v; }
    void setAnalyzePhrases(bool v) { analyzePhrases_ = v; }
    void setAnalyzeIntroOutro(bool v) { analyzeIntroOutro_ = v; }
    void setAnalyzeDownbeats(bool v) { analyzeDownbeats_ = v; }
    void setGenerateWaveform(bool v) { generateWaveform_ = v; }

private:
    bool analyzeBPM_ = true;
    bool analyzeKey_ = true;
    bool analyzeEnergy_ = true;
    bool analyzeLoudness_ = true;
    bool analyzeStructure_ = true;
    bool analyzePhrases_ = true;
    bool analyzeIntroOutro_ = true;
    bool analyzeDownbeats_ = true;
    bool generateWaveform_ = true;

    std::atomic<bool> cancelled_{false};
};

} // namespace BeatMate::Core
