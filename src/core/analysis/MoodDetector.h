#pragma once

#include <string>
#include <vector>

namespace BeatMate::Core {

class AudioTrack;
struct KeyResult;
struct EnergyResult;

enum class MoodType { Happy, Sad, Energetic, Calm, Aggressive, Melancholic, Euphoric, Dark };

struct MoodResult {
    MoodType mood = MoodType::Calm;
    std::string moodName;
    float confidence = 0.0f;
    float valence = 0.5f;    // 0=negative, 1=positive
    float arousal = 0.5f;    // 0=calm, 1=energetic
};

class MoodDetector {
public:
    MoodDetector();
    ~MoodDetector();

    MoodResult detect(const AudioTrack& track);

    static MoodResult classify(const KeyResult& keyResult, const EnergyResult& energyResult);
    static std::string moodToString(MoodType mood);
};

} // namespace BeatMate::Core
