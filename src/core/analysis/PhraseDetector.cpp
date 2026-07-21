#include "PhraseDetector.h"
#include "../audio/AudioTrack.h"
#include <cmath>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

PhraseDetector::PhraseDetector() = default;
PhraseDetector::~PhraseDetector() = default;

std::vector<Phrase> PhraseDetector::detect(const AudioTrack& track, double bpm) {
    spdlog::info("PhraseDetector: analyzing at {:.1f} BPM", bpm);

    if (bpm <= 0) return {};

    double beatDuration = 60.0 / bpm;
    double barDuration = beatDuration * 4.0;
    double duration = track.getDuration();

    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    int sr = monoTrack.getSampleRate();

    int barSamples = static_cast<int>(barDuration * sr);
    int totalBars = static_cast<int>(duration / barDuration);

    std::vector<float> barEnergy(totalBars, 0.0f);
    for (int b = 0; b < totalBars; ++b) {
        size_t start = b * barSamples;
        float rms = 0.0f;
        for (int i = 0; i < barSamples && start + i < monoTrack.getTotalSamples(); ++i) {
            float s = data[start + i];
            rms += s * s;
        }
        barEnergy[b] = std::sqrt(rms / barSamples);
    }

    std::vector<Phrase> phrases;
    int phraseStart = 0;

    int phraseBars = 8;
    for (int b = 0; b < totalBars; b += phraseBars) {
        int end = std::min(b + phraseBars, totalBars);

        float avgBefore = 0, avgAfter = 0;
        int countBefore = 0, countAfter = 0;
        for (int j = std::max(0, b - 4); j < b; ++j) {
            avgBefore += barEnergy[j]; countBefore++;
        }
        for (int j = b; j < std::min(totalBars, b + 4); ++j) {
            avgAfter += barEnergy[j]; countAfter++;
        }
        if (countBefore > 0) avgBefore /= countBefore;
        if (countAfter > 0) avgAfter /= countAfter;

        float energyChange = std::fabs(avgAfter - avgBefore);

        Phrase phrase;
        phrase.startTime = b * barDuration;
        phrase.endTime = end * barDuration;
        phrase.bars = end - b;

        if (phrase.bars == 8) phrase.type = "phrase8";
        else if (phrase.bars == 4) phrase.type = "phrase4";
        else if (phrase.bars == 16) phrase.type = "phrase16";
        else phrase.type = "phrase" + std::to_string(phrase.bars);

        phrase.confidence = std::clamp(energyChange * 10.0f, 0.3f, 1.0f);
        phrases.push_back(phrase);
    }

    spdlog::info("PhraseDetector: found {} phrases", phrases.size());
    return phrases;
}

} // namespace BeatMate::Core
