#include <algorithm>
#include "IntelligentCueCreator.h"
#include <spdlog/spdlog.h>
#include <cmath>

namespace BeatMate::Services::AI {

std::vector<Models::CuePoint> IntelligentCueCreator::createCues(const Core::AudioTrack& track, int64_t trackId) {
    std::vector<Models::CuePoint> cues;
    double duration = track.getDuration();

    double firstBeat = findFirstBeat(track);
    cues.push_back(Models::CuePoint(0, trackId, Models::CuePointType::HotCue, firstBeat, 0, "Intro", "#00FF00", 1));

    double drop = findDrop(track);
    if (drop > 0) cues.push_back(Models::CuePoint(0, trackId, Models::CuePointType::HotCue, drop, 0, "Drop", "#FF0000", 2));

    double breakdown = findBreakdown(track);
    if (breakdown > 0) cues.push_back(Models::CuePoint(0, trackId, Models::CuePointType::HotCue, breakdown, 0, "Breakdown", "#FFFF00", 3));

    double outro = findOutro(track);
    if (outro > 0) cues.push_back(Models::CuePoint(0, trackId, Models::CuePointType::HotCue, outro, 0, "Outro", "#0000FF", 4));

    spdlog::info("IntelligentCueCreator: Created {} cue points for track", cues.size());
    return cues;
}

double IntelligentCueCreator::findFirstBeat(const Core::AudioTrack& track) const {
    size_t numFrames = track.getNumFrames();
    int sr = track.getSampleRate();
    size_t windowSize = static_cast<size_t>(sr) / 10;

    for (size_t i = 0; i < std::min(numFrames, static_cast<size_t>(sr * 30)); i += windowSize) {
        float rms = 0.0f;
        for (size_t j = i; j < std::min(i + windowSize, numFrames); ++j) {
            float s = track.getSample(j);
            rms += s * s;
        }
        rms = std::sqrt(rms / windowSize);
        if (rms > 0.05f) return static_cast<double>(i) / sr;
    }
    return 0.0;
}

double IntelligentCueCreator::findDrop(const Core::AudioTrack& track) const {
    return track.getDuration() * 0.25;
}

double IntelligentCueCreator::findBreakdown(const Core::AudioTrack& track) const {
    return track.getDuration() * 0.5;
}

double IntelligentCueCreator::findOutro(const Core::AudioTrack& track) const {
    return track.getDuration() * 0.85;
}

}
