#include "MoodDetector.h"
#include "../audio/AudioTrack.h"
#include "KeyDetector.h"
#include "EnergyAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

MoodDetector::MoodDetector() = default;
MoodDetector::~MoodDetector() = default;

std::string MoodDetector::moodToString(MoodType mood) {
    switch (mood) {
        case MoodType::Happy: return "Happy";
        case MoodType::Sad: return "Sad";
        case MoodType::Energetic: return "Energetic";
        case MoodType::Calm: return "Calm";
        case MoodType::Aggressive: return "Aggressive";
        case MoodType::Melancholic: return "Melancholic";
        case MoodType::Euphoric: return "Euphoric";
        case MoodType::Dark: return "Dark";
        default: return "Unknown";
    }
}

MoodResult MoodDetector::detect(const AudioTrack& track) {
    spdlog::info("MoodDetector: analyzing {}", track.getFilePath());

    KeyDetector keyDet;
    auto keyResult = keyDet.detect(track);

    EnergyAnalyzer energyAn;
    auto energyResult = energyAn.analyze(track);

    auto result = classify(keyResult, energyResult);

    spdlog::info("MoodDetector: {} (valence={:.2f}, arousal={:.2f}, conf={:.0f}%)",
                 result.moodName, result.valence, result.arousal, result.confidence * 100);
    return result;
}

MoodResult MoodDetector::classify(const KeyResult& keyResult, const EnergyResult& energyResult) {
    MoodResult result;

    result.valence = keyResult.isMinor ? 0.3f : 0.7f;

    result.arousal = static_cast<float>(energyResult.overall) / 10.0f;

    if (energyResult.spectralCentroid > 3000) result.arousal += 0.1f;
    if (energyResult.spectralCentroid < 1500) result.arousal -= 0.1f;

    result.valence = std::clamp(result.valence, 0.0f, 1.0f);
    result.arousal = std::clamp(result.arousal, 0.0f, 1.0f);

    if (result.arousal > 0.7f && result.valence > 0.6f) {
        result.mood = MoodType::Euphoric;
    } else if (result.arousal > 0.7f && result.valence < 0.4f) {
        result.mood = MoodType::Aggressive;
    } else if (result.arousal > 0.5f && result.valence > 0.5f) {
        result.mood = MoodType::Happy;
    } else if (result.arousal > 0.5f && result.valence <= 0.5f) {
        result.mood = MoodType::Energetic;
    } else if (result.arousal <= 0.5f && result.valence > 0.5f) {
        result.mood = MoodType::Calm;
    } else if (result.arousal <= 0.3f && result.valence < 0.4f) {
        result.mood = MoodType::Melancholic;
    } else if (result.arousal <= 0.5f && result.valence < 0.3f) {
        result.mood = MoodType::Dark;
    } else {
        result.mood = MoodType::Sad;
    }

    result.moodName = moodToString(result.mood);
    result.confidence = 0.6f + keyResult.confidence * 0.2f;
    return result;
}

} // namespace BeatMate::Core
