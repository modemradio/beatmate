#pragma once

#include "HotCueManager.h"
#include "../analysis/StructureDetector.h"
#include <vector>
#include <string>

namespace BeatMate::Core {

class AudioTrack;

struct IntelligentCue {
    CuePoint cue;
    std::string reason;             // Why this position was chosen
    float importance = 0.0f;       // 0-1, how important this cue is
    std::string sectionType;       // "intro", "drop", "breakdown", etc.
    bool isDownbeat = false;       // Aligned to downbeat
    int phraseNumber = 0;          // Which phrase this falls in
};

struct IntelligentCueResult {
    std::vector<IntelligentCue> cues;
    int requestedCues = 8;
    float overallConfidence = 0.0f;
};

class IntelligentCueCreator {
public:
    IntelligentCueCreator();
    ~IntelligentCueCreator();

    IntelligentCueResult generateCues(const AudioTrack& track, int maxCues = 8);

    IntelligentCueResult generateCues(const AudioTrack& track,
                                        const std::vector<Section>& sections,
                                        const std::vector<double>& beats,
                                        double bpm,
                                        int maxCues = 8);

    void setPrioritizeDrops(bool v) { prioritizeDrops_ = v; }
    void setPrioritizeVocals(bool v) { prioritizeVocals_ = v; }
    void setSnapToDownbeat(bool v) { snapToDownbeat_ = v; }

private:
    struct CueCandidate {
        double position = 0.0;
        float score = 0.0f;
        std::string reason;
        std::string sectionType;
        uint32_t color = 0xFFFF0000;
    };

    std::vector<CueCandidate> generateCandidates(const AudioTrack& track,
                                                   const std::vector<Section>& sections,
                                                   const std::vector<double>& beats,
                                                   double bpm);

    float scoreEnergyTransition(const AudioTrack& track, double position);

    double snapToNearestBeat(double position, const std::vector<double>& beats, double bpm);

    std::vector<CueCandidate> selectOptimalCues(const std::vector<CueCandidate>& candidates,
                                                  int maxCues, double trackDuration);

    uint32_t colorForSection(const std::string& type);

    bool prioritizeDrops_ = true;
    bool prioritizeVocals_ = false;
    bool snapToDownbeat_ = true;
};

} // namespace BeatMate::Core
