#pragma once

#include <string>
#include <vector>

namespace BeatMate::Core {

class AudioTrack;

enum class SectionType { Intro, Verse, Chorus, Bridge, Drop, Breakdown, Buildup, Outro, Unknown };

struct Section {
    SectionType type = SectionType::Unknown;
    double startTime = 0.0;
    double endTime = 0.0;
    std::string label;
    float confidence = 0.0f;
};

class StructureDetector {
public:
    StructureDetector();
    ~StructureDetector();

    std::vector<Section> detect(const AudioTrack& track);

    static std::string sectionTypeToString(SectionType type);

private:
    std::vector<std::vector<float>> computeSelfSimilarity(const AudioTrack& track);

    std::vector<float> computeNoveltyCurve(const std::vector<std::vector<float>>& ssm);

    std::vector<double> findBoundaries(const std::vector<float>& novelty, double hopDuration);

    // uses pre-computed mono track to avoid repeated copies
    SectionType classifySection(const AudioTrack& monoTrack, double start, double end, bool isAlreadyMono);
};

}
