#pragma once

#include "StructureDetector.h"
#include <vector>
#include <string>

namespace BeatMate::Core {

class AudioTrack;

struct SongSection {
    SectionType type = SectionType::Unknown;
    double startTime = 0.0;
    double endTime = 0.0;
    std::string label;
    float energy = 0.0f;       // Average energy 0-1
    float confidence = 0.0f;
    int startBar = 0;
    int endBar = 0;
    int barCount = 0;
};

struct SongStructure {
    std::vector<SongSection> sections;
    double totalDuration = 0.0;
    int totalBars = 0;
    double bpm = 0.0;
    std::string formLabel;     // e.g., "AABA", "Verse-Chorus"
    bool hasIntro = false;
    bool hasOutro = false;
    bool hasDrop = false;
    float structureConfidence = 0.0f;
};

class SongStructureAnalyzerService {
public:
    SongStructureAnalyzerService();
    ~SongStructureAnalyzerService();

    SongStructure analyze(const AudioTrack& track, double bpm = 0.0);

    void setMinSectionBars(int bars) { minSectionBars_ = bars; }

private:
    std::vector<float> computeEnergyProfile(const AudioTrack& track, double barDuration);

    std::string identifyForm(const std::vector<SongSection>& sections);

    std::vector<SongSection> mergeSections(const std::vector<SongSection>& sections, int minBars);

    int minSectionBars_ = 4;
};

} // namespace BeatMate::Core
