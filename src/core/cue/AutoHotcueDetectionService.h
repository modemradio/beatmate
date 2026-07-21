#pragma once

#include "HotCueManager.h"
#include <vector>
#include <string>

namespace BeatMate::Core {

class AudioTrack;

struct AutoDetectedCue {
    CuePoint cue;
    std::string detectionMethod;    // "energy_peak", "spectral_change", "onset", "silence_end"
    float score = 0.0f;             // Detection confidence 0-1
    std::string description;        // Human-readable description
};

struct AutoHotcueResult {
    std::vector<AutoDetectedCue> detectedCues;
    int suggestedCount = 0;
    float overallConfidence = 0.0f;
};

class AutoHotcueDetectionService {
public:
    AutoHotcueDetectionService();
    ~AutoHotcueDetectionService();

    AutoHotcueResult detect(const AudioTrack& track, int maxCues = 8, double bpm = 0.0);

    void setDetectEnergyPeaks(bool v) { detectEnergyPeaks_ = v; }
    void setDetectSpectralChanges(bool v) { detectSpectralChanges_ = v; }
    void setDetectOnsets(bool v) { detectOnsets_ = v; }
    void setDetectSilenceEnd(bool v) { detectSilenceEnd_ = v; }

private:
    std::vector<AutoDetectedCue> findEnergyPeaks(const AudioTrack& track, double barDuration);

    std::vector<AutoDetectedCue> findSpectralChanges(const AudioTrack& track, double barDuration);

    std::vector<AutoDetectedCue> findSignificantOnsets(const AudioTrack& track, double barDuration);

    std::vector<AutoDetectedCue> findSilenceEnds(const AudioTrack& track);

    std::vector<AutoDetectedCue> deduplicateAndSelect(const std::vector<AutoDetectedCue>& candidates,
                                                        int maxCues, double minDistance);

    bool detectEnergyPeaks_ = true;
    bool detectSpectralChanges_ = true;
    bool detectOnsets_ = true;
    bool detectSilenceEnd_ = true;
};

} // namespace BeatMate::Core
