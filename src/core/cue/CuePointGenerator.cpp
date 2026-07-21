#include "CuePointGenerator.h"
#include "../audio/AudioTrack.h"
#include "../analysis/StructureDetector.h"
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

CuePointGenerator::CuePointGenerator() = default;
CuePointGenerator::~CuePointGenerator() = default;

std::vector<CuePoint> CuePointGenerator::generateCues(const AudioTrack& track, int maxCues) {
    StructureDetector detector;
    auto sections = detector.detect(track);

    std::vector<CuePoint> cues;
    uint32_t colors[] = {
        0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFF00,
        0xFFFF00FF, 0xFF00FFFF, 0xFFFF8000, 0xFF8000FF
    };

    int num = 1;
    for (auto& sec : sections) {
        if (num > maxCues) break;
        CuePoint cue;
        cue.number = num;
        cue.position = sec.startTime;
        cue.name = sec.label;
        cue.color = colors[(num - 1) % 8];
        cues.push_back(cue);
        num++;
    }

    spdlog::info("CuePointGenerator: generated {} cues", cues.size());
    return cues;
}

} // namespace BeatMate::Core
