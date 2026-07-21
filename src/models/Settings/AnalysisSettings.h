#pragma once

#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models::Settings {

enum class KeyDetectionMethod : int {
    EDM = 0,            // optimized for electronic music
    Classical = 1,      // optimized for classical/acoustic
    Hybrid = 2,         // balanced approach
    DeepLearning = 3    // ML-based detection
};

NLOHMANN_JSON_SERIALIZE_ENUM(KeyDetectionMethod, {
    { KeyDetectionMethod::EDM, "EDM" },
    { KeyDetectionMethod::Classical, "Classical" },
    { KeyDetectionMethod::Hybrid, "Hybrid" },
    { KeyDetectionMethod::DeepLearning, "DeepLearning" }
})

enum class AnalysisQuality : int {
    Fast = 0,
    Normal = 1,
    High = 2,
    Ultra = 3
};

NLOHMANN_JSON_SERIALIZE_ENUM(AnalysisQuality, {
    { AnalysisQuality::Fast, "Fast" },
    { AnalysisQuality::Normal, "Normal" },
    { AnalysisQuality::High, "High" },
    { AnalysisQuality::Ultra, "Ultra" }
})

enum class KeyNotation : int {
    Camelot = 0,        // "8A", "11B"
    OpenKey = 1,        // "Am", "G"
    Musical = 2,        // "A minor", "G major"
    MusicalSharp = 3    // "A#m", "Gb"
};

NLOHMANN_JSON_SERIALIZE_ENUM(KeyNotation, {
    { KeyNotation::Camelot, "Camelot" },
    { KeyNotation::OpenKey, "OpenKey" },
    { KeyNotation::Musical, "Musical" },
    { KeyNotation::MusicalSharp, "MusicalSharp" }
})

struct AnalysisSettings {
    double bpmRangeMin = 70.0;
    double bpmRangeMax = 180.0;
    bool bpmDoubleHalve = true;     // auto-correct double/half BPM

    KeyDetectionMethod keyDetectionMethod = KeyDetectionMethod::Hybrid;
    KeyNotation keyNotation = KeyNotation::Camelot;
    bool writeKeyToFile = false;

    AnalysisQuality analysisQuality = AnalysisQuality::Normal;

    bool autoAnalyze = true;
    bool autoAnalyzeOnImport = true;
    bool reAnalyzeOnChange = false;

    bool analyzeBPM = true;
    bool analyzeKey = true;
    bool analyzeEnergy = true;
    bool analyzeMood = true;
    bool analyzeBeats = true;
    bool analyzeSections = true;
    bool analyzeWaveform = true;
    bool analyzeLoudness = true;
    bool analyzeOnsets = true;
    bool analyzeSpectral = true;
    bool analyzeChromagram = true;
    bool analyzeMFCC = true;

    bool autoSetBeatGrid = true;
    bool lockBeatGridAfterAnalysis = false;
    int beatsPerBar = 4;

    bool autoSeparateStems = false;
    std::string stemModel = "htdemucs";
    std::string stemDevice = "auto";    // "auto", "cpu", "cuda"

    int maxConcurrentAnalysis = 2;

    bool writeAnalysisToFile = false;
    bool writeBPMTag = false;
    bool writeReplayGain = false;

    int waveformResolution = 256;       // samples per pixel
    int overviewResolution = 2048;

    AnalysisSettings() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AnalysisSettings,
        bpmRangeMin, bpmRangeMax, bpmDoubleHalve,
        keyDetectionMethod, keyNotation, writeKeyToFile,
        analysisQuality,
        autoAnalyze, autoAnalyzeOnImport, reAnalyzeOnChange,
        analyzeBPM, analyzeKey, analyzeEnergy, analyzeMood,
        analyzeBeats, analyzeSections, analyzeWaveform, analyzeLoudness,
        analyzeOnsets, analyzeSpectral, analyzeChromagram, analyzeMFCC,
        autoSetBeatGrid, lockBeatGridAfterAnalysis, beatsPerBar,
        autoSeparateStems, stemModel, stemDevice,
        maxConcurrentAnalysis,
        writeAnalysisToFile, writeBPMTag, writeReplayGain,
        waveformResolution, overviewResolution
    )
};

} // namespace BeatMate::Models::Settings
